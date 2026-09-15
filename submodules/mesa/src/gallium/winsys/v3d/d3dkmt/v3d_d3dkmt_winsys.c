/*
 * Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * V3D Gallium winsys for the ReactOS rpi5vc4 D3DKMT transport.
 *
 * Mesa's V3D driver is deliberately kept on its existing DRM UAPI structs.
 * This file terminates that ABI in user mode and translates only capabilities
 * implemented by vc4kmt.  Unsupported Linux sharing/perf interfaces fail
 * instead of being advertised.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "util/log.h"
#include "util/os_time.h"
#include "util/u_math.h"
#include "util/u_thread.h"

#include "broadcom/common/v3d_d3dkmt.h"
#include "broadcom/common/v3d_tfu.h"
#include "broadcom/common/v3d_tiling.h"
#include "v3d/v3d_screen.h"
#include "v3d_d3dkmt_public.h"

/*
 * Keep the Mesa port independent of ReactOS DDK headers.  These declarations
 * are the plain-C ABI exported by sdk/lib/vc4kmt; static assertions below
 * protect the data structures consumed across that boundary.
 */
typedef int32_t vc4kmt_status;
typedef struct vc4kmt_device VC4KMT_DEVICE;

typedef struct vc4kmt_bo {
   uint32_t allocation;
   uint32_t size;
   void *cpu_va;
   uint32_t gpu_va;
} VC4KMT_BO;

typedef struct vc4kmt_fence {
   uint32_t sync_object;
   uint64_t value;
   volatile const uint64_t *cpu_value;
} VC4KMT_FENCE;

typedef struct vc4kmt_resource {
   uint32_t allocation;
   uint32_t flags;
} VC4KMT_RESOURCE;

typedef struct vc4kmt_cl_submit {
   uint32_t bcl_start;
   uint32_t bcl_end;
   uint32_t rcl_start;
   uint32_t rcl_end;
   uint32_t qma;
   uint32_t qms;
   uint32_t qts;
} VC4KMT_CL_SUBMIT;

typedef struct vc4kmt_tfu_submit {
   uint32_t regs[12];
} VC4KMT_TFU_SUBMIT;

typedef struct vc4kmt_csd_submit {
   uint32_t cfg[8];
} VC4KMT_CSD_SUBMIT;

typedef struct vc4kmt_info {
   uint32_t magic;
   uint32_t op;
   uint32_t v3d_ready;
   uint32_t v3d_version;
   uint32_t hub_ident[4];
   uint32_t core_ident[3];
   uint32_t slab_gpu_va;
   uint64_t slab_physical;
   uint32_t slab_size;
   uint32_t screen_width;
   uint32_t screen_height;
   uint32_t screen_pitch;
   uint32_t abi_version;
   uint32_t caps;
   uint32_t node_count;
   uint32_t max_pending_submits;
   uint32_t allocation_alignment;
   uint32_t tfu_register_count;
   uint32_t csd_config_count;
   uint32_t linear_format_mask;
   uint32_t reserved[8];
} VC4KMT_INFO;

vc4kmt_status vc4kmt_open(VC4KMT_DEVICE **device);
void vc4kmt_close(VC4KMT_DEVICE *device);
const VC4KMT_INFO *vc4kmt_info(const VC4KMT_DEVICE *device);
vc4kmt_status vc4kmt_bo_create(VC4KMT_DEVICE *device, uint32_t size,
                               VC4KMT_BO *bo);
vc4kmt_status vc4kmt_bo_create_ex(VC4KMT_DEVICE *device, uint32_t size,
                                  uint32_t flags, VC4KMT_BO *bo);
vc4kmt_status vc4kmt_bo_map(VC4KMT_DEVICE *device, VC4KMT_BO *bo,
                            void **cpu_va);
vc4kmt_status vc4kmt_bo_invalidate(VC4KMT_DEVICE *device,
                                   const VC4KMT_BO *bo,
                                   uint32_t offset, uint32_t length);
uint32_t vc4kmt_bo_gpuva(const VC4KMT_BO *bo);
vc4kmt_status vc4kmt_shared_resource_info(VC4KMT_DEVICE *device,
                                          uint32_t global_share,
                                          void *runtime_data,
                                          uint32_t runtime_capacity,
                                          uint32_t *runtime_size);
vc4kmt_status vc4kmt_bo_open_shared(VC4KMT_DEVICE *device,
                                    uint32_t global_share,
                                    uint32_t size,
                                    VC4KMT_BO *bo,
                                    uint32_t *resource);
vc4kmt_status vc4kmt_bo_close_shared(VC4KMT_DEVICE *device,
                                     VC4KMT_BO *bo,
                                     uint32_t resource);
vc4kmt_status vc4kmt_primary_gpuva(VC4KMT_DEVICE *device,
                                   uint32_t width, uint32_t height,
                                   uint32_t pitch, uint32_t *gpu_va);
uint32_t vc4kmt_primary_allocation(const VC4KMT_DEVICE *device);
vc4kmt_status vc4kmt_primary_present(VC4KMT_DEVICE *device, void *window);
void vc4kmt_primary_invalidate(VC4KMT_DEVICE *device);
vc4kmt_status vc4kmt_bo_destroy(VC4KMT_DEVICE *device, VC4KMT_BO *bo);
vc4kmt_status vc4kmt_submit_cl(VC4KMT_DEVICE *device,
                               const VC4KMT_CL_SUBMIT *submit,
                               VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_submit_cl_resources(VC4KMT_DEVICE *device,
                                         const VC4KMT_CL_SUBMIT *submit,
                                         const VC4KMT_RESOURCE *resources,
                                         uint32_t resource_count,
                                         VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_submit_cl_resources_ex(VC4KMT_DEVICE *device,
                                            const VC4KMT_CL_SUBMIT *submit,
                                            uint32_t flags,
                                            const VC4KMT_RESOURCE *resources,
                                            uint32_t resource_count,
                                            VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_submit_tfu(VC4KMT_DEVICE *device,
                                const VC4KMT_TFU_SUBMIT *submit,
                                VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_submit_tfu_resources(VC4KMT_DEVICE *device,
                                          const VC4KMT_TFU_SUBMIT *submit,
                                          const VC4KMT_RESOURCE *resources,
                                          uint32_t resource_count,
                                          VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_submit_csd(VC4KMT_DEVICE *device,
                                const VC4KMT_CSD_SUBMIT *submit,
                                VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_submit_csd_resources(VC4KMT_DEVICE *device,
                                          const VC4KMT_CSD_SUBMIT *submit,
                                          const VC4KMT_RESOURCE *resources,
                                          uint32_t resource_count,
                                          VC4KMT_FENCE *fence);
vc4kmt_status vc4kmt_wait(VC4KMT_DEVICE *device,
                          const VC4KMT_FENCE *fence, uint32_t timeout_ms);
vc4kmt_status vc4kmt_wait_async(VC4KMT_DEVICE *device,
                                const VC4KMT_FENCE *fence,
                                void *completion_event);
vc4kmt_status vc4kmt_wait_gpu_many(VC4KMT_DEVICE *device, uint32_t engine,
                                   const VC4KMT_FENCE *fences,
                                   uint32_t fence_count);
void vc4kmt_fence_destroy(VC4KMT_DEVICE *device, VC4KMT_FENCE *fence);

_Static_assert(offsetof(VC4KMT_BO, cpu_va) == 8,
               "vc4kmt BO pointer offset mismatch");
_Static_assert(offsetof(VC4KMT_BO, gpu_va) == 8 + sizeof(void *),
               "vc4kmt BO GPU VA offset mismatch");
_Static_assert(offsetof(VC4KMT_FENCE, value) == 8,
               "vc4kmt fence value offset mismatch");
_Static_assert(offsetof(VC4KMT_FENCE, cpu_value) == 16,
               "vc4kmt fence pointer offset mismatch");
_Static_assert(sizeof(VC4KMT_CL_SUBMIT) == 7 * sizeof(uint32_t),
               "vc4kmt CL ABI mismatch");

#define VC4KMT_CAP_CL_SUBMIT             (1u << 0)
#define VC4KMT_CAP_TFU_SUBMIT            (1u << 1)
#define VC4KMT_CAP_CSD_SUBMIT            (1u << 2)
#define VC4KMT_CAP_CACHE_FLUSH           (1u << 3)
#define VC4KMT_RESOURCE_CPU_DIRTY        (1u << 0)
#define VC4KMT_CL_FLAG_FLUSH_CACHE       (1u << 0)
#define VC4KMT_BO_CREATE_CPU_CACHED      (1u << 0)
#define VC4KMT_ENGINE_3D                 0u
#define VC4KMT_ENGINE_TFU                1u
#define VC4KMT_ENGINE_CSD                2u
#define VC4KMT_ENGINE_COUNT              3u
#define VC4KMT_STATUS_IO_TIMEOUT         ((vc4kmt_status)0xc00000b5u)

#define DWM_DX_SURFACE_INFO_MAGIC         0x53585744u
#define DWM_DX_SURFACE_INFO_VERSION       1u
#define DWM_DX_FORMAT_B8G8R8A8_UNORM      87u

struct dwm_dx_shared_surface_info {
   uint32_t magic;
   uint32_t version;
   uint32_t width;
   uint32_t height;
   uint32_t pitch;
   uint32_t format;
};

#define V3D_D3DKMT_MAX_DEVICES 8
#define V3D_D3DKMT_FD_BASE 0x40000000
#define V3D_D3DKMT_MAX_SUBMIT_RESOURCES 4096u
#define V3D_D3DKMT_INFINITE_MS UINT32_MAX

/* Four-byte TFU copies use the R32F type to preserve every source bit. */
#define V3D71_TFU_TEXTURE_FORMAT_R32F 29u

struct v3d_d3dkmt_bo {
   VC4KMT_BO kmt;
   struct v3d_d3dkmt_fence_ref *last_fence;
   uint32_t shared_resource;
   bool allocated;
   bool cpu_dirty;
   bool cpu_cache_stale;
};

struct v3d_d3dkmt_syncobj {
   struct v3d_d3dkmt_fence_ref *fence;
   bool allocated;
   bool signaled;
};

struct v3d_d3dkmt_fence_ref {
   VC4KMT_FENCE kmt;
   uint32_t references;
   uint32_t engine;
   bool signaled;
};

struct v3d_d3dkmt_device {
   VC4KMT_DEVICE *kmt;
   const VC4KMT_INFO *info;
   mtx_t lock;
   struct v3d_d3dkmt_bo *bos;
   uint32_t bo_capacity;
   struct v3d_d3dkmt_syncobj *syncobjs;
   uint32_t syncobj_capacity;
   VC4KMT_RESOURCE *submit_resources;
   uint32_t submit_resource_capacity;
   uint32_t *submit_resource_hash;
   uint32_t submit_resource_hash_capacity;
};

static once_flag registry_once = ONCE_FLAG_INIT;
static mtx_t registry_lock;
static struct v3d_d3dkmt_device *registry[V3D_D3DKMT_MAX_DEVICES];

static void
v3d_d3dkmt_registry_init(void)
{
   (void)mtx_init(&registry_lock, mtx_plain);
}

static struct v3d_d3dkmt_device *
v3d_d3dkmt_device_lookup(int fd)
{
   struct v3d_d3dkmt_device *device = NULL;

   call_once(&registry_once, v3d_d3dkmt_registry_init);
   if (fd < V3D_D3DKMT_FD_BASE ||
       fd >= V3D_D3DKMT_FD_BASE + V3D_D3DKMT_MAX_DEVICES)
      return NULL;

   mtx_lock(&registry_lock);
   device = registry[fd - V3D_D3DKMT_FD_BASE];
   mtx_unlock(&registry_lock);
   return device;
}

static bool
v3d_d3dkmt_grow(void **array, uint32_t *capacity, size_t element_size,
                 uint32_t minimum)
{
   uint32_t new_capacity = MAX2(*capacity, 16u);
   void *new_array;

   while (new_capacity <= minimum) {
      if (new_capacity > UINT32_MAX / 2)
         return false;
      new_capacity *= 2;
   }

   new_array = realloc(*array, (size_t)new_capacity * element_size);
   if (!new_array)
      return false;

   memset((uint8_t *)new_array + (size_t)*capacity * element_size, 0,
          (size_t)(new_capacity - *capacity) * element_size);
   *array = new_array;
   *capacity = new_capacity;
   return true;
}

static uint32_t
v3d_d3dkmt_alloc_bo_handle(struct v3d_d3dkmt_device *device)
{
   uint32_t handle;

   for (handle = 1; handle < device->bo_capacity; handle++) {
      if (!device->bos[handle].allocated)
         return handle;
   }

   if (!v3d_d3dkmt_grow((void **)&device->bos, &device->bo_capacity,
                        sizeof(*device->bos), device->bo_capacity))
      return 0;

   return handle;
}

static uint32_t
v3d_d3dkmt_alloc_syncobj_handle(struct v3d_d3dkmt_device *device)
{
   uint32_t handle;

   for (handle = 1; handle < device->syncobj_capacity; handle++) {
      if (!device->syncobjs[handle].allocated)
         return handle;
   }

   if (!v3d_d3dkmt_grow((void **)&device->syncobjs,
                        &device->syncobj_capacity,
                        sizeof(*device->syncobjs),
                        device->syncobj_capacity))
      return 0;

   return handle;
}

static struct v3d_d3dkmt_bo *
v3d_d3dkmt_bo_lookup_locked(struct v3d_d3dkmt_device *device,
                            uint32_t handle)
{
   if (!handle || handle >= device->bo_capacity ||
       !device->bos[handle].allocated)
      return NULL;
   return &device->bos[handle];
}

static uint32_t
v3d_d3dkmt_submit_handle(uint32_t handle)
{
   return handle & V3D_D3DKMT_SUBMIT_HANDLE_MASK;
}

static int
v3d_d3dkmt_resolve_submit_resources_locked(
   struct v3d_d3dkmt_device *device, const uint32_t *bo_handles,
   uint32_t bo_handle_count, VC4KMT_RESOURCE **resources_out,
   uint32_t *resource_count_out)
{
   VC4KMT_RESOURCE *resources;
   uint32_t resource_count = 0;
   uint32_t hash_capacity = 16;

   *resources_out = NULL;
   *resource_count_out = 0;
   if (bo_handle_count > V3D_D3DKMT_MAX_SUBMIT_RESOURCES ||
       (bo_handle_count && !bo_handles)) {
      errno = EINVAL;
      return -1;
   }
   if (!bo_handle_count)
      return 0;

   if (device->submit_resource_capacity < bo_handle_count) {
      VC4KMT_RESOURCE *new_resources =
         realloc(device->submit_resources,
                 (size_t)bo_handle_count * sizeof(*new_resources));
      if (!new_resources) {
         errno = ENOMEM;
         return -1;
      }
      device->submit_resources = new_resources;
      device->submit_resource_capacity = bo_handle_count;
   }
   resources = device->submit_resources;

   while (hash_capacity < bo_handle_count * 2)
      hash_capacity *= 2;
   if (device->submit_resource_hash_capacity < hash_capacity) {
      uint32_t *new_hash =
         realloc(device->submit_resource_hash,
                 (size_t)hash_capacity * sizeof(*new_hash));
      if (!new_hash) {
         errno = ENOMEM;
         return -1;
      }
      device->submit_resource_hash = new_hash;
      device->submit_resource_hash_capacity = hash_capacity;
   }
   memset(device->submit_resource_hash, 0,
          (size_t)hash_capacity * sizeof(*device->submit_resource_hash));

   for (uint32_t i = 0; i < bo_handle_count; i++) {
      struct v3d_d3dkmt_bo *bo;
      uint32_t handle = v3d_d3dkmt_submit_handle(bo_handles[i]);
      uint32_t hash_slot;
      uint32_t resource_index;

      if (!handle)
         continue;
      bo = v3d_d3dkmt_bo_lookup_locked(device, handle);
      if (!bo || !bo->kmt.allocation) {
         errno = EINVAL;
         return -1;
      }

      hash_slot = bo->kmt.allocation * 2654435761u & (hash_capacity - 1);
      for (;;) {
         uint32_t hash_value = device->submit_resource_hash[hash_slot];

         if (!hash_value) {
            resource_index = resource_count++;
            resources[resource_index].allocation = bo->kmt.allocation;
            resources[resource_index].flags = 0;
            device->submit_resource_hash[hash_slot] = resource_index + 1;
            break;
         }
         resource_index = hash_value - 1;
         if (resources[resource_index].allocation == bo->kmt.allocation)
            break;
         hash_slot = (hash_slot + 1) & (hash_capacity - 1);
      }
      if (bo->cpu_dirty &&
          !(resources[resource_index].flags & VC4KMT_RESOURCE_CPU_DIRTY)) {
         resources[resource_index].flags |= VC4KMT_RESOURCE_CPU_DIRTY;
      }
   }

   *resources_out = resources;
   *resource_count_out = resource_count;
   return 0;
}

static void
v3d_d3dkmt_finish_submit_resources_locked(
   struct v3d_d3dkmt_device *device, const uint32_t *bo_handles,
   uint32_t bo_handle_count)
{
   for (uint32_t i = 0; i < bo_handle_count; i++) {
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(
            device, v3d_d3dkmt_submit_handle(bo_handles[i]));

      if (bo) {
         bo->cpu_dirty = false;
         if (bo_handles[i] & V3D_D3DKMT_SUBMIT_HANDLE_WRITE)
            bo->cpu_cache_stale = true;
      }
   }
}

static struct v3d_d3dkmt_syncobj *
v3d_d3dkmt_syncobj_lookup_locked(struct v3d_d3dkmt_device *device,
                                 uint32_t handle)
{
   if (!handle || handle >= device->syncobj_capacity ||
       !device->syncobjs[handle].allocated)
      return NULL;
   return &device->syncobjs[handle];
}

static uint32_t
v3d_d3dkmt_timeout_ms(int64_t absolute_timeout_ns)
{
   uint64_t now;
   uint64_t remaining;

   if (absolute_timeout_ns == INT64_MAX)
      return V3D_D3DKMT_INFINITE_MS;
   if (absolute_timeout_ns <= 0)
      return 0;

   now = os_time_get_nano();
   if ((uint64_t)absolute_timeout_ns <= now)
      return 0;

   remaining = (uint64_t)absolute_timeout_ns - now;
   remaining = DIV_ROUND_UP(remaining, 1000000ull);
   return remaining >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)remaining;
}

static int
v3d_d3dkmt_wait_fence_locked(struct v3d_d3dkmt_device *device,
                             const VC4KMT_FENCE *fence,
                             uint32_t timeout_ms)
{
   vc4kmt_status status = vc4kmt_wait(device->kmt, fence, timeout_ms);

   if (status >= 0)
      return 0;

   if (status == VC4KMT_STATUS_IO_TIMEOUT) {
      errno = ETIME;
      return -ETIME;
   }

   errno = EIO;
   return -EIO;
}

static struct v3d_d3dkmt_fence_ref *
v3d_d3dkmt_fence_create(const VC4KMT_FENCE *fence, uint32_t engine)
{
   struct v3d_d3dkmt_fence_ref *reference = malloc(sizeof(*reference));

   if (!reference)
      return NULL;

   reference->kmt = *fence;
   reference->references = 1;
   reference->engine = engine;
   reference->signaled = false;
   return reference;
}

static struct v3d_d3dkmt_fence_ref *
v3d_d3dkmt_fence_reference(struct v3d_d3dkmt_fence_ref *fence)
{
   assert(fence && fence->references != UINT32_MAX);
   fence->references++;
   return fence;
}

static void
v3d_d3dkmt_fence_release_locked(struct v3d_d3dkmt_device *device,
                                struct v3d_d3dkmt_fence_ref **fence)
{
   struct v3d_d3dkmt_fence_ref *reference = *fence;

   if (!reference)
      return;

   assert(reference->references);
   if (!--reference->references) {
      vc4kmt_fence_destroy(device->kmt, &reference->kmt);
      free(reference);
   }
   *fence = NULL;
}

static int
v3d_d3dkmt_wait_fence_ref_locked(
   struct v3d_d3dkmt_device *device,
   struct v3d_d3dkmt_fence_ref *fence,
   uint32_t timeout_ms)
{
   int result;

   if (fence->signaled)
      return 0;

   result = v3d_d3dkmt_wait_fence_locked(device, &fence->kmt, timeout_ms);
   if (!result)
      fence->signaled = true;
   return result;
}

static int
v3d_d3dkmt_wait_syncobj_locked(struct v3d_d3dkmt_device *device,
                               uint32_t handle, uint32_t timeout_ms)
{
   struct v3d_d3dkmt_syncobj *syncobj =
      v3d_d3dkmt_syncobj_lookup_locked(device, handle);

   if (!syncobj) {
      errno = EINVAL;
      return -EINVAL;
   }
   if (syncobj->signaled || !syncobj->fence)
      return syncobj->signaled ? 0 : -ETIME;

   int result = v3d_d3dkmt_wait_fence_ref_locked(device, syncobj->fence,
                                                  timeout_ms);
   if (!result) {
      v3d_d3dkmt_fence_release_locked(device, &syncobj->fence);
      syncobj->signaled = true;
   }
   return result;
}

static int
v3d_d3dkmt_add_gpu_dependency(struct v3d_d3dkmt_fence_ref *fence,
                              uint32_t engine,
                              VC4KMT_FENCE dependencies[VC4KMT_ENGINE_COUNT],
                              bool used[VC4KMT_ENGINE_COUNT])
{
   if (!fence || fence->signaled || fence->engine == engine)
      return 0;
   if (fence->engine >= VC4KMT_ENGINE_COUNT) {
      errno = EINVAL;
      return -EINVAL;
   }
   if (!used[fence->engine] ||
       dependencies[fence->engine].value < fence->kmt.value)
      dependencies[fence->engine] = fence->kmt;
   used[fence->engine] = true;
   return 0;
}

static int
v3d_d3dkmt_wait_submit_dependencies_locked(
   struct v3d_d3dkmt_device *device, uint32_t engine,
   const uint32_t *sync_handles, uint32_t sync_handle_count,
   const uint32_t *bo_handles, uint32_t bo_handle_count)
{
   VC4KMT_FENCE dependencies[VC4KMT_ENGINE_COUNT];
   VC4KMT_FENCE pending[VC4KMT_ENGINE_COUNT];
   bool used[VC4KMT_ENGINE_COUNT] = { false };
   uint32_t pending_count = 0;

   for (uint32_t i = 0; i < sync_handle_count; i++) {
      struct v3d_d3dkmt_syncobj *syncobj;

      if (!sync_handles[i])
         continue;
      syncobj = v3d_d3dkmt_syncobj_lookup_locked(device, sync_handles[i]);
      if (!syncobj) {
         errno = EINVAL;
         return -EINVAL;
      }
      if (syncobj->signaled)
         continue;
      if (!syncobj->fence) {
         errno = ETIME;
         return -ETIME;
      }
      if (v3d_d3dkmt_add_gpu_dependency(syncobj->fence, engine,
                                        dependencies, used))
         return -1;
   }

   for (uint32_t i = 0; i < bo_handle_count; i++) {
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(
            device, v3d_d3dkmt_submit_handle(bo_handles[i]));
      struct v3d_d3dkmt_fence_ref *fence;

      if (!bo || !(fence = bo->last_fence))
         continue;
      if (v3d_d3dkmt_add_gpu_dependency(fence, engine, dependencies, used))
         return -1;
   }

   for (uint32_t source_engine = 0;
        source_engine < VC4KMT_ENGINE_COUNT; source_engine++) {
      if (used[source_engine])
         pending[pending_count++] = dependencies[source_engine];
   }
   if (!pending_count)
      return 0;

   if (vc4kmt_wait_gpu_many(device->kmt, engine, pending, pending_count) < 0) {
      errno = EIO;
      return -EIO;
   }
   return 0;
}

static int
v3d_d3dkmt_store_submit_fence_locked(struct v3d_d3dkmt_device *device,
                                     uint32_t out_sync,
                                     const uint32_t *bo_handles,
                                     uint32_t bo_handle_count,
                                     uint32_t engine,
                                     const VC4KMT_FENCE *fence)
{
   struct v3d_d3dkmt_syncobj *syncobj;
   struct v3d_d3dkmt_fence_ref *reference;

   if (out_sync) {
      syncobj = v3d_d3dkmt_syncobj_lookup_locked(device, out_sync);
      if (!syncobj) {
         errno = EINVAL;
         return -1;
      }
   }

   reference = v3d_d3dkmt_fence_create(fence, engine);
   if (!reference) {
      VC4KMT_FENCE discarded = *fence;

      (void)v3d_d3dkmt_wait_fence_locked(device, fence,
                                         V3D_D3DKMT_INFINITE_MS);
      vc4kmt_fence_destroy(device->kmt, &discarded);
      errno = ENOMEM;
      return -1;
   }

   if (out_sync) {
      v3d_d3dkmt_fence_release_locked(device, &syncobj->fence);
      syncobj->fence = v3d_d3dkmt_fence_reference(reference);
      syncobj->signaled = false;
   }

   for (uint32_t i = 0; i < bo_handle_count; i++) {
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(
            device, v3d_d3dkmt_submit_handle(bo_handles[i]));
      if (!bo)
         continue;
      v3d_d3dkmt_fence_release_locked(device, &bo->last_fence);
      bo->last_fence = v3d_d3dkmt_fence_reference(reference);
   }

   v3d_d3dkmt_fence_release_locked(device, &reference);
   return 0;
}

static bool
v3d_d3dkmt_submit_out_sync_valid_locked(struct v3d_d3dkmt_device *device,
                                        uint32_t out_sync)
{
   if (out_sync && !v3d_d3dkmt_syncobj_lookup_locked(device, out_sync)) {
      errno = EINVAL;
      return false;
   }

   return true;
}

int
v3d_d3dkmt_open(void)
{
   struct v3d_d3dkmt_device *device;
   vc4kmt_status status;
   int fd = -1;

   device = calloc(1, sizeof(*device));
   if (!device)
      return -1;

   status = vc4kmt_open(&device->kmt);
   if (status < 0)
      goto fail;

   device->info = vc4kmt_info(device->kmt);
   if (!device->info || !device->info->v3d_ready ||
       device->info->v3d_version != 71 ||
       !(device->info->caps & VC4KMT_CAP_CL_SUBMIT))
      goto fail;

   if (mtx_init(&device->lock, mtx_plain) != thrd_success)
      goto fail;

   if (!v3d_d3dkmt_grow((void **)&device->bos, &device->bo_capacity,
                        sizeof(*device->bos), 1) ||
       !v3d_d3dkmt_grow((void **)&device->syncobjs,
                        &device->syncobj_capacity,
                        sizeof(*device->syncobjs), 1))
      goto fail_lock;

   call_once(&registry_once, v3d_d3dkmt_registry_init);
   mtx_lock(&registry_lock);
   for (int i = 0; i < V3D_D3DKMT_MAX_DEVICES; i++) {
      if (!registry[i]) {
         registry[i] = device;
         fd = V3D_D3DKMT_FD_BASE + i;
         break;
      }
   }
   mtx_unlock(&registry_lock);

   if (fd != -1)
      return fd;

fail_lock:
   mtx_destroy(&device->lock);
fail:
   if (device->kmt)
      vc4kmt_close(device->kmt);
   free(device->syncobjs);
   free(device->submit_resource_hash);
   free(device->submit_resources);
   free(device->bos);
   free(device);
   return -1;
}

void
v3d_d3dkmt_close(int fd)
{
   struct v3d_d3dkmt_device *device;

   call_once(&registry_once, v3d_d3dkmt_registry_init);
   if (fd < V3D_D3DKMT_FD_BASE ||
       fd >= V3D_D3DKMT_FD_BASE + V3D_D3DKMT_MAX_DEVICES)
      return;

   mtx_lock(&registry_lock);
   device = registry[fd - V3D_D3DKMT_FD_BASE];
   registry[fd - V3D_D3DKMT_FD_BASE] = NULL;
   mtx_unlock(&registry_lock);
   if (!device)
      return;

   mtx_lock(&device->lock);
   for (uint32_t i = 1; i < device->bo_capacity; i++) {
      if (!device->bos[i].allocated)
         continue;
      if (device->bos[i].last_fence) {
         (void)v3d_d3dkmt_wait_fence_ref_locked(
            device, device->bos[i].last_fence, V3D_D3DKMT_INFINITE_MS);
         v3d_d3dkmt_fence_release_locked(device,
                                         &device->bos[i].last_fence);
      }
      (void)vc4kmt_bo_destroy(device->kmt, &device->bos[i].kmt);
   }
   for (uint32_t i = 1; i < device->syncobj_capacity; i++) {
      if (!device->syncobjs[i].allocated || !device->syncobjs[i].fence)
         continue;
      (void)v3d_d3dkmt_wait_fence_ref_locked(
         device, device->syncobjs[i].fence, V3D_D3DKMT_INFINITE_MS);
      v3d_d3dkmt_fence_release_locked(device,
                                      &device->syncobjs[i].fence);
   }
   mtx_unlock(&device->lock);

   vc4kmt_close(device->kmt);
   mtx_destroy(&device->lock);
   free(device->syncobjs);
   free(device->submit_resource_hash);
   free(device->submit_resources);
   free(device->bos);
   free(device);
}

void *
v3d_d3dkmt_bo_map(int fd, uint32_t handle)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_bo *bo;
   void *map = NULL;

   if (!device)
      return NULL;

   mtx_lock(&device->lock);
   bo = v3d_d3dkmt_bo_lookup_locked(device, handle);
   if (bo && vc4kmt_bo_map(device->kmt, &bo->kmt, &map) < 0)
      map = NULL;
   mtx_unlock(&device->lock);
   return map;
}

int
v3d_d3dkmt_bo_prepare_cpu_access(int fd, uint32_t handle, int write)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_bo *bo;
   vc4kmt_status status = -1;

   if (!device)
      return -1;

   mtx_lock(&device->lock);
   bo = v3d_d3dkmt_bo_lookup_locked(device, handle);
   if (bo) {
      status = 0;
      if (bo->cpu_cache_stale) {
         status = vc4kmt_bo_invalidate(device->kmt, &bo->kmt,
                                       0, bo->kmt.size);
         if (status >= 0)
            bo->cpu_cache_stale = false;
      }
      if (status >= 0 && write)
         bo->cpu_dirty = true;
   }
   mtx_unlock(&device->lock);
   return status;
}

int
v3d_d3dkmt_bo_copy_cpu_contents(int fd, uint32_t source_handle,
                                 uint32_t destination_handle, uint32_t size)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_bo *source;
   struct v3d_d3dkmt_bo *destination;
   void *source_map = NULL;
   void *destination_map = NULL;
   int result = -1;

   if (!device)
      return -1;

   mtx_lock(&device->lock);
   source = v3d_d3dkmt_bo_lookup_locked(device, source_handle);
   destination = v3d_d3dkmt_bo_lookup_locked(device, destination_handle);
   if (!source || !destination || size > source->kmt.size ||
       size > destination->kmt.size)
      goto done;

   if (source->cpu_cache_stale) {
      result = 0;
      goto done;
   }

   if (destination->cpu_cache_stale &&
       vc4kmt_bo_invalidate(device->kmt, &destination->kmt,
                            0, destination->kmt.size) < 0)
      goto done;

   destination->cpu_cache_stale = false;
   if (vc4kmt_bo_map(device->kmt, &source->kmt, &source_map) < 0 ||
       vc4kmt_bo_map(device->kmt, &destination->kmt,
                     &destination_map) < 0)
      goto done;

   memcpy(destination_map, source_map, size);
   destination->cpu_dirty = true;
   result = 1;

done:
   mtx_unlock(&device->lock);
   return result;
}

int
v3d_d3dkmt_bo_mark_external_dirty(int fd, uint32_t handle)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_bo *bo;
   int result = -1;

   if (!device)
      return -1;

   mtx_lock(&device->lock);
   bo = v3d_d3dkmt_bo_lookup_locked(device, handle);
   if (bo) {
      bo->cpu_dirty = true;
      bo->cpu_cache_stale = true;
      result = 0;
   }
   mtx_unlock(&device->lock);
   return result;
}

int
v3d_d3dkmt_present_linear(int fd, uintptr_t window, uint32_t source_handle,
                           uint32_t out_sync, uint32_t source_offset,
                           uint32_t source_stride,
                           uint32_t source_padded_height,
                           uint32_t source_tiling, uint32_t source_size,
                           uint32_t destination_x, uint32_t destination_y,
                           uint32_t width, uint32_t height,
                           uint32_t screen_width, uint32_t screen_height)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_bo *source;
   uint64_t source_end;
   uint64_t destination_offset;
   uint32_t primary_gpu_va;
   uint32_t primary_allocation;
   uint32_t primary_pitch;
   VC4KMT_RESOURCE resources[2];
   VC4KMT_TFU_SUBMIT submit = { 0 };
   VC4KMT_FENCE fence;
   int result = -1;

   if (!device || !window || !source_handle || !out_sync || !width || !height ||
       width > UINT16_MAX || height > UINT16_MAX ||
       screen_width > UINT16_MAX ||
       destination_x > screen_width || destination_y > screen_height ||
       width > screen_width - destination_x ||
       height > screen_height - destination_y ||
       source_tiling > V3D_TILING_UIF_XOR ||
       source_padded_height < height ||
       (uint64_t)source_stride * source_padded_height > source_size ||
       source_stride < width * 4 || (source_stride & 3)) {
      errno = EINVAL;
      return -1;
   }

   primary_pitch = screen_width * 4;
   source_end = (uint64_t)source_offset + source_size;
   destination_offset = (uint64_t)destination_y * primary_pitch +
                        (uint64_t)destination_x * 4;

   mtx_lock(&device->lock);
   source = v3d_d3dkmt_bo_lookup_locked(device, source_handle);
   if (!source || source_end > source->kmt.size ||
       destination_offset > UINT32_MAX) {
      errno = EINVAL;
      goto done;
   }
   if (!v3d_d3dkmt_syncobj_lookup_locked(device, out_sync)) {
      errno = EINVAL;
      goto done;
   }

   if (v3d_d3dkmt_wait_submit_dependencies_locked(
          device, VC4KMT_ENGINE_TFU, NULL, 0, &source_handle, 1))
      goto done;

   if (vc4kmt_primary_gpuva(device->kmt, screen_width, screen_height,
                            primary_pitch, &primary_gpu_va) < 0) {
      errno = EIO;
      goto done;
   }
   if ((uint64_t)primary_gpu_va + destination_offset > UINT32_MAX ||
       (uint64_t)source->kmt.gpu_va + source_offset > UINT32_MAX) {
      errno = EOVERFLOW;
      goto done;
   }
   primary_allocation = vc4kmt_primary_allocation(device->kmt);
   if (!source->kmt.allocation || !primary_allocation) {
      errno = EINVAL;
      goto done;
   }
   resources[0].allocation = source->kmt.allocation;
   resources[0].flags = source->cpu_dirty ? VC4KMT_RESOURCE_CPU_DIRTY : 0;
   resources[1].allocation = primary_allocation;
   resources[1].flags = 0;

   /* Use the same input layout and UIF block-row stride as v3dx_tfu.
    * The compositor keeps its render target tiled; scanout stays raster. */
   uint32_t input_format = source_tiling == V3D_TILING_RASTER ?
      V3D71_TFU_ICFG_FORMAT_RASTER :
      V3D71_TFU_ICFG_FORMAT_LINEARTILE + source_tiling - V3D_TILING_LINEARTILE;
   submit.regs[0] = (input_format << V3D71_TFU_ICFG_IFORMAT_SHIFT) |
                    (V3D71_TFU_TEXTURE_FORMAT_R32F << V3D71_TFU_ICFG_OTYPE_SHIFT);
   submit.regs[1] = source->kmt.gpu_va + source_offset;
   if (source_tiling == V3D_TILING_RASTER)
      submit.regs[3] = source_stride / 4;
   else if (source_tiling == V3D_TILING_UIF_NO_XOR ||
            source_tiling == V3D_TILING_UIF_XOR)
      submit.regs[3] = source_padded_height / (2 * v3d_utile_height(4));
   submit.regs[5] =
      (V3D71_TFU_IOC_FORMAT_RASTER << V3D71_TFU_IOC_FORMAT_SHIFT) |
      ((primary_pitch / 4) << V3D71_TFU_IOC_STRIDE_SHIFT);
   submit.regs[6] = primary_gpu_va + (uint32_t)destination_offset;
   submit.regs[7] = (height << 16) | width;

   vc4kmt_status submit_status =
      vc4kmt_submit_tfu_resources(device->kmt, &submit, resources,
                                  ARRAY_SIZE(resources), &fence);
   if (submit_status < 0) {
      vc4kmt_primary_invalidate(device->kmt);
      errno = EIO;
      goto done;
   }
   source->cpu_dirty = false;
   result = v3d_d3dkmt_store_submit_fence_locked(device, out_sync,
                                                   &source_handle, 1,
                                                   VC4KMT_ENGINE_TFU, &fence);
   if (!result) {
      vc4kmt_status present_status =
         vc4kmt_primary_present(device->kmt, (void *)window);
      if (present_status != 0) {
         errno = EIO;
         result = -1;
      }
   }

done:
   mtx_unlock(&device->lock);
   return result;
}

int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   uint32_t new_handle;

   if (!device || !handle || (flags & ~DRM_SYNCOBJ_CREATE_SIGNALED)) {
      errno = EINVAL;
      return -EINVAL;
   }

   mtx_lock(&device->lock);
   new_handle = v3d_d3dkmt_alloc_syncobj_handle(device);
   if (!new_handle) {
      mtx_unlock(&device->lock);
      errno = ENOMEM;
      return -ENOMEM;
   }

   memset(&device->syncobjs[new_handle], 0,
          sizeof(device->syncobjs[new_handle]));
   device->syncobjs[new_handle].allocated = true;
   device->syncobjs[new_handle].signaled =
      (flags & DRM_SYNCOBJ_CREATE_SIGNALED) != 0;
   *handle = new_handle;
   mtx_unlock(&device->lock);
   return 0;
}

int
drmSyncobjDestroy(int fd, uint32_t handle)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_syncobj *syncobj;

   if (!device) {
      errno = EINVAL;
      return -EINVAL;
   }

   mtx_lock(&device->lock);
   syncobj = v3d_d3dkmt_syncobj_lookup_locked(device, handle);
   if (!syncobj) {
      mtx_unlock(&device->lock);
      errno = EINVAL;
      return -EINVAL;
   }
   v3d_d3dkmt_fence_release_locked(device, &syncobj->fence);
   memset(syncobj, 0, sizeof(*syncobj));
   mtx_unlock(&device->lock);
   return 0;
}

int
drmSyncobjWait(int fd, const uint32_t *handles, unsigned num_handles,
               int64_t timeout_nsec, unsigned flags,
               uint32_t *first_signaled)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   uint32_t timeout_ms = v3d_d3dkmt_timeout_ms(timeout_nsec);
   int result = 0;

   if (!device || !handles || !num_handles ||
       (flags & ~DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)) {
      errno = EINVAL;
      return -EINVAL;
   }

   mtx_lock(&device->lock);
   for (unsigned i = 0; i < num_handles; i++) {
      result = v3d_d3dkmt_wait_syncobj_locked(device, handles[i], timeout_ms);
      if (result) {
         if (!(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) && result == -ETIME)
            continue;
         break;
      }
      if (first_signaled)
         *first_signaled = i;
      if (!(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL))
         break;
   }
   mtx_unlock(&device->lock);

   if (result)
      errno = result == -ETIME ? ETIME : EIO;
   return result;
}

int
v3d_d3dkmt_syncobj_signal_event(int fd, uint32_t handle, void *event)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_syncobj *syncobj;
   int result = 0;

   if (!device || !event) {
      errno = EINVAL;
      return -EINVAL;
   }

   mtx_lock(&device->lock);
   syncobj = v3d_d3dkmt_syncobj_lookup_locked(device, handle);
   if (!syncobj) {
      result = -EINVAL;
   } else if (syncobj->signaled) {
      if (!SetEvent((HANDLE)event))
         result = -EIO;
   } else if (!syncobj->fence) {
      result = -ETIME;
   } else {
      if (vc4kmt_wait_async(device->kmt, &syncobj->fence->kmt,
                            event) < 0)
         result = -EIO;
   }
   mtx_unlock(&device->lock);

   if (result)
      errno = -result;
   return result;
}

int
v3d_d3dkmt_syncobj_clone(int fd, uint32_t source, uint32_t *destination)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   struct v3d_d3dkmt_syncobj *source_syncobj;
   uint32_t handle;

   if (!device || !destination) {
      errno = EINVAL;
      return -EINVAL;
   }

   mtx_lock(&device->lock);
   source_syncobj = v3d_d3dkmt_syncobj_lookup_locked(device, source);
   handle = source_syncobj ? v3d_d3dkmt_alloc_syncobj_handle(device) : 0;
   if (!source_syncobj || !handle) {
      mtx_unlock(&device->lock);
      errno = source_syncobj ? ENOMEM : EINVAL;
      return source_syncobj ? -ENOMEM : -EINVAL;
   }

   /* Growing the syncobj table may have moved it. */
   source_syncobj = v3d_d3dkmt_syncobj_lookup_locked(device, source);
   assert(source_syncobj);
   device->syncobjs[handle] = *source_syncobj;
   if (source_syncobj->fence)
      device->syncobjs[handle].fence =
         v3d_d3dkmt_fence_reference(source_syncobj->fence);
   device->syncobjs[handle].allocated = true;
   *destination = handle;
   mtx_unlock(&device->lock);
   return 0;
}

int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
   (void)fd;
   (void)handle;
   (void)sync_file_fd;
   errno = EOPNOTSUPP;
   return -EOPNOTSUPP;
}

int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
   (void)fd;
   (void)handle;
   if (sync_file_fd)
      *sync_file_fd = -1;
   errno = EOPNOTSUPP;
   return -EOPNOTSUPP;
}

int
drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
   (void)fd;
   (void)prime_fd;
   (void)handle;
   errno = EOPNOTSUPP;
   return -EOPNOTSUPP;
}

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
   (void)fd;
   (void)handle;
   (void)flags;
   if (prime_fd)
      *prime_fd = -1;
   errno = EOPNOTSUPP;
   return -EOPNOTSUPP;
}

static int
v3d_d3dkmt_get_param(struct v3d_d3dkmt_device *device,
                     struct drm_v3d_get_param *param)
{
   switch (param->param) {
   case DRM_V3D_PARAM_V3D_UIFCFG:
      param->value = 0;
      return 0;
   case DRM_V3D_PARAM_V3D_HUB_IDENT1:
      param->value = device->info->hub_ident[1];
      return 0;
   case DRM_V3D_PARAM_V3D_HUB_IDENT2:
      param->value = device->info->hub_ident[2];
      return 0;
   case DRM_V3D_PARAM_V3D_HUB_IDENT3:
      param->value = device->info->hub_ident[3];
      return 0;
   case DRM_V3D_PARAM_V3D_CORE0_IDENT0:
      param->value = device->info->core_ident[0];
      return 0;
   case DRM_V3D_PARAM_V3D_CORE0_IDENT1:
      param->value = device->info->core_ident[1];
      return 0;
   case DRM_V3D_PARAM_V3D_CORE0_IDENT2:
      param->value = device->info->core_ident[2];
      return 0;
   case DRM_V3D_PARAM_SUPPORTS_TFU:
      param->value = !!(device->info->caps & VC4KMT_CAP_TFU_SUBMIT);
      return 0;
   case DRM_V3D_PARAM_SUPPORTS_CSD:
      param->value = !!(device->info->caps & VC4KMT_CAP_CSD_SUBMIT);
      return 0;
   case DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH:
      param->value = !!(device->info->caps & VC4KMT_CAP_CACHE_FLUSH);
      return 0;
   case DRM_V3D_PARAM_SUPPORTS_PERFMON:
   case DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT:
   case DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE:
   case DRM_V3D_PARAM_SUPPORTS_SUPER_PAGES:
   case DRM_V3D_PARAM_MAX_PERF_COUNTERS:
      param->value = 0;
      return 0;
   case DRM_V3D_PARAM_GLOBAL_RESET_COUNTER:
   case DRM_V3D_PARAM_CONTEXT_RESET_COUNTER:
   default:
      errno = EOPNOTSUPP;
      return -1;
   }
}

static int
v3d_d3dkmt_submit_cl_locked(struct v3d_d3dkmt_device *device,
                            const struct drm_v3d_submit_cl *submit)
{
   const uint32_t *bo_handles = (const uint32_t *)(uintptr_t)submit->bo_handles;
   VC4KMT_RESOURCE *resources = NULL;
   uint32_t resource_count = 0;
   VC4KMT_CL_SUBMIT kmt_submit = {
      .bcl_start = submit->bcl_start,
      .bcl_end = submit->bcl_end,
      .rcl_start = submit->rcl_start,
      .rcl_end = submit->rcl_end,
      .qma = submit->qma,
      .qms = submit->qms,
      .qts = submit->qts,
   };
   VC4KMT_FENCE fence;

   if ((submit->flags & ~DRM_V3D_SUBMIT_CL_FLUSH_CACHE) ||
       submit->extensions) {
      errno = EOPNOTSUPP;
      return -1;
   }
   if (!v3d_d3dkmt_submit_out_sync_valid_locked(device, submit->out_sync))
      return -1;

   const uint32_t sync_handles[] = {
      submit->in_sync_bcl, submit->in_sync_rcl,
   };
   if (v3d_d3dkmt_wait_submit_dependencies_locked(
          device, VC4KMT_ENGINE_3D, sync_handles, ARRAY_SIZE(sync_handles),
          bo_handles, submit->bo_handle_count))
      return -1;

   if (v3d_d3dkmt_resolve_submit_resources_locked(
          device, bo_handles, submit->bo_handle_count,
          &resources, &resource_count))
      return -1;
   uint32_t kmt_flags =
      (submit->flags & DRM_V3D_SUBMIT_CL_FLUSH_CACHE) ?
      VC4KMT_CL_FLAG_FLUSH_CACHE : 0;
   vc4kmt_status submit_status =
      vc4kmt_submit_cl_resources_ex(device->kmt, &kmt_submit, kmt_flags,
                                    resources, resource_count, &fence);
   if (submit_status < 0) {
      mesa_loge_once("V3D CL submit failed: status=%08x resources=%u",
                     (unsigned)submit_status, resource_count);
      errno = EIO;
      return -1;
   }
   v3d_d3dkmt_finish_submit_resources_locked(
      device, bo_handles, submit->bo_handle_count);

   int result = v3d_d3dkmt_store_submit_fence_locked(
      device, submit->out_sync, bo_handles, submit->bo_handle_count,
      VC4KMT_ENGINE_3D, &fence);
   return result;
}

static int
v3d_d3dkmt_submit_tfu_locked(struct v3d_d3dkmt_device *device,
                             const struct drm_v3d_submit_tfu *submit)
{
   const uint32_t handles[] = {
      submit->bo_handles[0], submit->bo_handles[1],
      submit->bo_handles[2], submit->bo_handles[3],
   };
   VC4KMT_TFU_SUBMIT kmt_submit = {
      .regs = {
         submit->icfg, submit->iia, submit->ica, submit->iis,
         submit->iua, submit->v71.ioc, submit->ioa, submit->ios,
         submit->coef[0], submit->coef[1], submit->coef[2], submit->coef[3],
      },
   };
   VC4KMT_RESOURCE *resources = NULL;
   uint32_t resource_count = 0;
   VC4KMT_FENCE fence;

   if (submit->flags || submit->extensions) {
      errno = EOPNOTSUPP;
      return -1;
   }
   if (!v3d_d3dkmt_submit_out_sync_valid_locked(device, submit->out_sync))
      return -1;
   if (v3d_d3dkmt_wait_submit_dependencies_locked(
          device, VC4KMT_ENGINE_TFU, &submit->in_sync, 1,
          handles, ARRAY_SIZE(handles)))
      return -1;
   if (v3d_d3dkmt_resolve_submit_resources_locked(
          device, handles, ARRAY_SIZE(handles),
          &resources, &resource_count))
      return -1;
   vc4kmt_status submit_status =
      vc4kmt_submit_tfu_resources(device->kmt, &kmt_submit, resources,
                                  resource_count, &fence);
   if (submit_status < 0) {
      mesa_loge_once("V3D TFU submit failed: status=%08x resources=%u",
                     (unsigned)submit_status, resource_count);
      errno = EIO;
      return -1;
   }
   v3d_d3dkmt_finish_submit_resources_locked(
      device, handles, ARRAY_SIZE(handles));

   int result = v3d_d3dkmt_store_submit_fence_locked(
      device, submit->out_sync, handles, ARRAY_SIZE(handles),
      VC4KMT_ENGINE_TFU, &fence);
   return result;
}

static int
v3d_d3dkmt_submit_csd_locked(struct v3d_d3dkmt_device *device,
                             const struct drm_v3d_submit_csd *submit)
{
   const uint32_t *bo_handles = (const uint32_t *)(uintptr_t)submit->bo_handles;
   VC4KMT_RESOURCE *resources = NULL;
   uint32_t resource_count = 0;
   VC4KMT_CSD_SUBMIT kmt_submit = { 0 };
   VC4KMT_FENCE fence;

   if (submit->flags || submit->extensions) {
      errno = EOPNOTSUPP;
      return -1;
   }
   if (!v3d_d3dkmt_submit_out_sync_valid_locked(device, submit->out_sync))
      return -1;
   memcpy(kmt_submit.cfg, submit->cfg, sizeof(submit->cfg));
   if (v3d_d3dkmt_wait_submit_dependencies_locked(
          device, VC4KMT_ENGINE_CSD, &submit->in_sync, 1,
          bo_handles, submit->bo_handle_count))
      return -1;
   if (v3d_d3dkmt_resolve_submit_resources_locked(
          device, bo_handles, submit->bo_handle_count,
          &resources, &resource_count))
      return -1;
   vc4kmt_status submit_status =
      vc4kmt_submit_csd_resources(device->kmt, &kmt_submit, resources,
                                  resource_count, &fence);
   if (submit_status < 0) {
      errno = EIO;
      return -1;
   }
   v3d_d3dkmt_finish_submit_resources_locked(
      device, bo_handles, submit->bo_handle_count);

   int result = v3d_d3dkmt_store_submit_fence_locked(
      device, submit->out_sync, bo_handles, submit->bo_handle_count,
      VC4KMT_ENGINE_CSD, &fence);
   return result;
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
   struct v3d_d3dkmt_device *device = v3d_d3dkmt_device_lookup(fd);
   int result = -1;

   if (!device || !arg) {
      errno = EINVAL;
      return -1;
   }

   mtx_lock(&device->lock);
   switch (request) {
   case DRM_IOCTL_V3D_GET_PARAM:
      result = v3d_d3dkmt_get_param(device, arg);
      break;
   case DRM_IOCTL_V3D_CREATE_BO: {
      struct drm_v3d_create_bo *create = arg;
      uint32_t handle;
      uint32_t flags;
      struct v3d_d3dkmt_bo *bo;

      if (create->flags & ~V3D_D3DKMT_CREATE_BO_CPU_CACHED) {
         errno = EINVAL;
         break;
      }
      handle = v3d_d3dkmt_alloc_bo_handle(device);
      if (!handle) {
         errno = ENOMEM;
         break;
      }
      flags = (create->flags & V3D_D3DKMT_CREATE_BO_CPU_CACHED) ?
              VC4KMT_BO_CREATE_CPU_CACHED : 0;
      bo = &device->bos[handle];
      memset(bo, 0, sizeof(*bo));
      if (vc4kmt_bo_create_ex(device->kmt, create->size, flags,
                              &bo->kmt) < 0) {
         errno = ENOMEM;
         break;
      }
      bo->allocated = true;
      create->handle = handle;
      create->offset = vc4kmt_bo_gpuva(&bo->kmt);
      result = 0;
      break;
   }
   case DRM_IOCTL_V3D_GET_BO_OFFSET: {
      struct drm_v3d_get_bo_offset *get = arg;
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(device, get->handle);
      if (!bo) {
         errno = EINVAL;
         break;
      }
      get->offset = vc4kmt_bo_gpuva(&bo->kmt);
      result = 0;
      break;
   }
   case DRM_IOCTL_V3D_MMAP_BO: {
      struct drm_v3d_mmap_bo *map = arg;
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(device, map->handle);
      if (!bo) {
         errno = EINVAL;
         break;
      }
      map->offset = map->handle;
      result = 0;
      break;
   }
   case DRM_IOCTL_V3D_WAIT_BO: {
      struct drm_v3d_wait_bo *wait = arg;
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(device, wait->handle);
      if (!bo) {
         errno = EINVAL;
         break;
      }
      if (!bo->last_fence) {
         result = 0;
         break;
      }
      result = v3d_d3dkmt_wait_fence_ref_locked(
         device, bo->last_fence,
         wait->timeout_ns == UINT64_MAX ? V3D_D3DKMT_INFINITE_MS :
         (uint32_t)MIN2(DIV_ROUND_UP(wait->timeout_ns, 1000000ull),
                        (uint64_t)UINT32_MAX - 1));
      if (result) {
         result = -1;
      } else {
         v3d_d3dkmt_fence_release_locked(device, &bo->last_fence);
      }
      break;
   }
   case DRM_IOCTL_GEM_CLOSE: {
      struct drm_gem_close *close_bo = arg;
      struct v3d_d3dkmt_bo *bo =
         v3d_d3dkmt_bo_lookup_locked(device, close_bo->handle);
      int wait_result = 0;

      if (!bo) {
         errno = EINVAL;
         break;
      }
      if (bo->last_fence)
         wait_result = v3d_d3dkmt_wait_fence_ref_locked(
            device, bo->last_fence, V3D_D3DKMT_INFINITE_MS);
      v3d_d3dkmt_fence_release_locked(device, &bo->last_fence);
      vc4kmt_status status = bo->shared_resource ?
         vc4kmt_bo_close_shared(device->kmt, &bo->kmt,
                                bo->shared_resource) :
         vc4kmt_bo_destroy(device->kmt, &bo->kmt);
      if (status < 0) {
         errno = EIO;
         break;
      }
      memset(bo, 0, sizeof(*bo));
      if (wait_result) {
         errno = EIO;
         break;
      }
      result = 0;
      break;
   }
   case DRM_IOCTL_V3D_SUBMIT_CL:
      result = v3d_d3dkmt_submit_cl_locked(device, arg);
      break;
   case DRM_IOCTL_V3D_SUBMIT_TFU:
      result = v3d_d3dkmt_submit_tfu_locked(device, arg);
      break;
   case DRM_IOCTL_V3D_SUBMIT_CSD:
      result = v3d_d3dkmt_submit_csd_locked(device, arg);
      break;
   case DRM_IOCTL_GEM_OPEN: {
      struct drm_gem_open *open_bo = arg;
      struct dwm_dx_shared_surface_info info;
      vc4kmt_status status;
      uint32_t runtime_size = 0;
      uint32_t handle;
      uint64_t size;

      memset(&info, 0, sizeof(info));
      status = vc4kmt_shared_resource_info(device->kmt, open_bo->name,
                                           &info, sizeof(info),
                                           &runtime_size);
      if (status < 0 ||
          runtime_size != sizeof(info) ||
          info.magic != DWM_DX_SURFACE_INFO_MAGIC ||
          info.version != DWM_DX_SURFACE_INFO_VERSION ||
          info.width == 0 || info.height == 0 ||
          info.width > UINT32_MAX / 4 ||
          info.pitch < info.width * 4 || (info.pitch & 3) != 0 ||
          info.format != DWM_DX_FORMAT_B8G8R8A8_UNORM) {
         errno = EINVAL;
         break;
      }
      size = (uint64_t)info.pitch * info.height;
      if (size == 0 || size > UINT32_MAX) {
         errno = EOVERFLOW;
         break;
      }

      handle = v3d_d3dkmt_alloc_bo_handle(device);
      if (!handle) {
         errno = ENOMEM;
         break;
      }

      struct v3d_d3dkmt_bo *bo = &device->bos[handle];
      memset(bo, 0, sizeof(*bo));
      status = vc4kmt_bo_open_shared(device->kmt, open_bo->name,
                                     (uint32_t)size, &bo->kmt,
                                     &bo->shared_resource);
      if (status < 0) {
         memset(bo, 0, sizeof(*bo));
         errno = EIO;
         break;
      }
      bo->allocated = true;
      open_bo->handle = handle;
      open_bo->size = size;
      result = 0;
      break;
   }
   case DRM_IOCTL_GEM_FLINK:
   case DRM_IOCTL_V3D_PERFMON_CREATE:
   case DRM_IOCTL_V3D_PERFMON_DESTROY:
   case DRM_IOCTL_V3D_PERFMON_GET_VALUES:
   case DRM_IOCTL_V3D_PERFMON_GET_COUNTER:
   case DRM_IOCTL_V3D_SUBMIT_CPU:
   default:
      errno = EOPNOTSUPP;
      result = -1;
      break;
   }
   mtx_unlock(&device->lock);
   return result;
}

bool
v3d_d3dkmt_shared_surface_info(struct pipe_screen *screen,
                               uintptr_t shared_handle,
                               uint32_t *width,
                               uint32_t *height,
                               uint32_t *pitch)
{
   struct v3d_d3dkmt_device *device;
   struct dwm_dx_shared_surface_info info;
   uint32_t runtime_size = 0;
   vc4kmt_status status;

   if (!screen || !shared_handle || shared_handle > UINT32_MAX ||
       !width || !height || !pitch)
      return false;

   device = v3d_d3dkmt_device_lookup(v3d_screen(screen)->fd);
   if (!device)
      return false;

   memset(&info, 0, sizeof(info));
   mtx_lock(&device->lock);
   status = vc4kmt_shared_resource_info(device->kmt,
                                        (uint32_t)shared_handle,
                                        &info, sizeof(info),
                                        &runtime_size);
   mtx_unlock(&device->lock);

   if (status < 0 || runtime_size != sizeof(info) ||
       info.magic != DWM_DX_SURFACE_INFO_MAGIC ||
       info.version != DWM_DX_SURFACE_INFO_VERSION ||
       !info.width || !info.height || info.width > UINT32_MAX / 4 ||
       info.pitch < info.width * 4 || (info.pitch & 3) != 0 ||
       info.format != DWM_DX_FORMAT_B8G8R8A8_UNORM)
      return false;

   *width = info.width;
   *height = info.height;
   *pitch = info.pitch;
   return true;
}

struct pipe_screen *
v3d_d3dkmt_screen_create(const struct pipe_screen_config *config)
{
   int fd = v3d_d3dkmt_open();

   if (fd < 0)
      return NULL;

   return v3d_screen_create(fd, config, NULL);
}
