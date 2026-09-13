/*
 * Copyright 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "intel_virtio_priv.h"
#include "i915_proto.h"

struct virtio_gem_execbuffer_params {
   struct intel_virtio_device *dev;
   struct drm_i915_gem_execbuffer2 *exec;

   struct drm_virtgpu_execbuffer_syncobj *in_syncobjs;
   unsigned num_in_syncobjs;

   struct drm_virtgpu_execbuffer_syncobj *out_syncobjs;
   unsigned num_out_syncobjs;

   uint32_t *bo_handles;
   unsigned num_bo_handles;
};

static int
i915_virtio_gem_execbuffer2_submit(struct virtio_gem_execbuffer_params *params)
{
   struct intel_virtio_device *dev = params->dev;
   struct i915_shmem *shmem = to_i915_shmem(dev->vdrm->shmem);
   struct drm_i915_gem_execbuffer2 *exec = params->exec;
   struct drm_i915_gem_exec_object2 *buffers = (void *)(uintptr_t)exec->buffers_ptr;
   struct drm_i915_gem_relocation_entry *relocs;
   struct i915_ccmd_gem_execbuffer2_rsp *rsp;
   uint64_t allowed_flags = 0, flags = exec->flags;

   if (exec->rsvd1 < 64 &&
       (p_atomic_read(&shmem->banned_ctx_mask) & (1ULL << exec->rsvd1)))
      return EIO;

   unsigned relocs_count = 0;

   for (int i = 0; i < exec->buffer_count; i++)
       relocs_count += buffers[i].relocation_count;

   size_t buffers_size = sizeof(*buffers) * exec->buffer_count;
   size_t relocations_size = sizeof(struct drm_i915_gem_relocation_entry) * relocs_count;

   unsigned req_len = sizeof(struct i915_ccmd_gem_execbuffer2_req);
   req_len += buffers_size + relocations_size;

   uint8_t buf[req_len];
   struct i915_ccmd_gem_execbuffer2_req *req = (void *)buf;
   memcpy(req->payload, buffers, buffers_size);

   uint32_t bo_handles[exec->buffer_count + 1];
   buffers = (void *)req->payload;
   relocs = (void *)(req->payload + buffers_size);

   for (int i = 0; i < exec->buffer_count; i++) {
      memcpy(relocs, (void *)(uintptr_t)buffers[i].relocs_ptr,
             sizeof(*relocs) * buffers[i].relocation_count);

      relocs += buffers[i].relocation_count;

      bo_handles[i] = buffers[i].handle;
      buffers[i].handle = vdrm_handle_to_res_id(dev->vdrm, buffers[i].handle);
   }

   params->bo_handles = bo_handles;
   params->num_bo_handles = exec->buffer_count;

   allowed_flags |= I915_EXEC_RING_MASK;
   allowed_flags |= I915_EXEC_CONSTANTS_MASK;
   allowed_flags |= I915_EXEC_GEN7_SOL_RESET;
   allowed_flags |= I915_EXEC_NO_RELOC;
   allowed_flags |= I915_EXEC_HANDLE_LUT;
   allowed_flags |= I915_EXEC_BSD_MASK << I915_EXEC_BSD_SHIFT;
   allowed_flags |= I915_EXEC_BATCH_FIRST;

   /* XXX: sanity-check flags, might be removed in a release version */
   if (flags & ~(allowed_flags | I915_EXEC_FENCE_ARRAY | I915_EXEC_USE_EXTENSIONS)) {
      mesa_loge("unsupported flags");
      return EINVAL;
   }

   req->hdr = I915_CCMD(GEM_EXECBUFFER2, req_len);
   req->relocs_count = relocs_count;
   req->buffer_count = exec->buffer_count;
   req->batch_start_offset = exec->batch_start_offset;
   req->batch_len = exec->batch_len;
   req->context_id = exec->rsvd1;
   req->flags = flags & allowed_flags;

   rsp = vdrm_alloc_rsp(dev->vdrm, &req->hdr, sizeof(*rsp));

   struct vdrm_execbuf_params p = {
      .req = &req->hdr,
      .ring_idx = 1 + (flags & I915_EXEC_RING_MASK),
      .in_syncobjs = params->in_syncobjs,
      .num_in_syncobjs = params->num_in_syncobjs,
      .out_syncobjs = params->out_syncobjs,
      .num_out_syncobjs = params->num_out_syncobjs,
      .handles = params->bo_handles,
      .num_handles = params->num_bo_handles,
   };

   return vdrm_execbuf(dev->vdrm, &p);
}

int
i915_virtio_gem_execbuffer2(struct intel_virtio_device *dev,
                            struct drm_i915_gem_execbuffer2 *exec)
{
   unsigned num_waits = 0, num_signals = 0, num_fences = 0, i, w, s;
   struct drm_i915_gem_execbuffer_ext_timeline_fences *ext;
   struct virtio_gem_execbuffer_params params;
   struct drm_i915_gem_exec_fence *fences;
   uint64_t *syncobj_values = NULL;
   int ret;

   memset(&params, 0, sizeof(params));

   if (exec->flags & I915_EXEC_USE_EXTENSIONS) {
      ext = (void *)(uintptr_t)exec->cliprects_ptr;

      if (ext->base.name != DRM_I915_GEM_EXECBUFFER_EXT_TIMELINE_FENCES) {
         mesa_loge("unsupported extension");
         return EINVAL;
      }

      if (ext->base.next_extension) {
         mesa_loge("unsupported extension");
         return EINVAL;
      }

      num_fences = ext->fence_count;
      fences = (void *)(uintptr_t)ext->handles_ptr;
      syncobj_values = (void *)(uintptr_t)ext->values_ptr;
   } else if (exec->flags & I915_EXEC_FENCE_ARRAY) {
      fences = (void *)(uintptr_t)exec->cliprects_ptr;
      num_fences = exec->num_cliprects;
   }

   for (i = 0; i < num_fences; i++) {
      if (fences[i].flags & I915_EXEC_FENCE_WAIT)
         num_waits++;

      if (fences[i].flags & I915_EXEC_FENCE_SIGNAL)
         num_signals++;
   }

   if (num_waits) {
      params.in_syncobjs = calloc(sizeof(*params.in_syncobjs), num_waits);
      if (!params.in_syncobjs) {
         ret = ENOMEM;
         goto out;
      }
   }

   if (num_signals) {
      params.out_syncobjs = calloc(sizeof(*params.out_syncobjs), num_signals);
      if (!params.out_syncobjs) {
         ret = ENOMEM;
         goto out;
      }
   }

   for (i = 0, w = 0, s = 0; i < num_fences; i++) {
      if (fences[i].flags & I915_EXEC_FENCE_WAIT) {
         params.in_syncobjs[w].handle = fences[i].handle;
         if (syncobj_values)
            params.in_syncobjs[w].point = syncobj_values[i];
         w++;
      }

      if (fences[i].flags & I915_EXEC_FENCE_SIGNAL) {
         params.out_syncobjs[s].handle = fences[i].handle;
         if (syncobj_values)
            params.out_syncobjs[s].point = syncobj_values[i];
         s++;
      }

      if (!(fences[i].flags & (I915_EXEC_FENCE_WAIT | I915_EXEC_FENCE_SIGNAL))) {
         mesa_loge("invalid fence flags");
         ret = ENOMEM;
         goto out;
      }
   }

   params.dev = dev;
   params.exec = exec;
   params.num_in_syncobjs = num_waits;
   params.num_out_syncobjs = num_signals;

   ret = i915_virtio_gem_execbuffer2_submit(&params);
out:
   free(params.out_syncobjs);
   free(params.in_syncobjs);

   return ret;
}
