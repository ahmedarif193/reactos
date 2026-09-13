/*
 * Copyright 2026 Ahmed Arif <arif193@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VideoCore IV Gallium winsys for the ReactOS rpi3vc4 D3DKMT transport.
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "util/u_math.h"
#include "util/u_thread.h"
#include "vc4/vc4_screen.h"
#include "vc4_d3dkmt_public.h"
#include "drm-uapi/drm.h"
#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/vc4_drm.h"

typedef int32_t rpi3vc4kmt_status;
typedef struct rpi3vc4kmt_device RPI3VC4KMT_DEVICE;

typedef struct rpi3vc4kmt_bo {
   uint32_t allocation;
   uint32_t size;
   void *cpu_va;
   uint32_t flags;
} RPI3VC4KMT_BO;

#define RPI3VC4KMT_BO_SHADER 0x00000001u
#define RPI3VC4KMT_BO_CPU_DIRTY 0x00000002u

typedef struct rpi3vc4kmt_fence {
   uint32_t sync_object;
   uint64_t value;
   volatile const uint64_t *cpu_value;
} RPI3VC4KMT_FENCE;

typedef struct rpi3vc4_submit_rcl_surface {
   uint32_t hindex;
   uint32_t offset;
   uint16_t bits;
   uint16_t flags;
} RPI3VC4_SUBMIT_RCL_SURFACE;

typedef struct rpi3vc4kmt_submit_cl {
   const void *bin_cl;
   uint32_t bin_cl_size;
   const void *shader_rec;
   uint32_t shader_rec_size;
   uint32_t shader_rec_count;
   const void *uniforms;
   uint32_t uniforms_size;
   const RPI3VC4KMT_BO *const *bos;
   uint32_t bo_count;
   uint16_t width;
   uint16_t height;
   uint8_t min_x_tile;
   uint8_t min_y_tile;
   uint8_t max_x_tile;
   uint8_t max_y_tile;
   RPI3VC4_SUBMIT_RCL_SURFACE color_read;
   RPI3VC4_SUBMIT_RCL_SURFACE color_write;
   RPI3VC4_SUBMIT_RCL_SURFACE zs_read;
   RPI3VC4_SUBMIT_RCL_SURFACE zs_write;
   RPI3VC4_SUBMIT_RCL_SURFACE msaa_color_write;
   RPI3VC4_SUBMIT_RCL_SURFACE msaa_zs_write;
   uint32_t clear_color[2];
   uint32_t clear_z;
   uint8_t clear_s;
   uint32_t flags;
} RPI3VC4KMT_SUBMIT_CL;

typedef struct rpi3vc4_info {
   uint32_t magic;
   uint32_t op;
   uint32_t size;
   uint32_t abi_version;
   int32_t initialization_status;
   uint32_t caps;
   uint32_t v3d_ready;
   uint32_t ident0;
   uint32_t ident1;
   uint32_t ident2;
   uint64_t v3d_physical;
   uint32_t screen_width;
   uint32_t screen_height;
   uint32_t screen_pitch;
   int32_t render_test_status;
   uint32_t render_test_pixel;
   uint32_t reserved[6];
} RPI3VC4_INFO;

rpi3vc4kmt_status rpi3vc4kmt_open(RPI3VC4KMT_DEVICE **device);
void rpi3vc4kmt_close(RPI3VC4KMT_DEVICE *device);
const RPI3VC4_INFO *rpi3vc4kmt_info(const RPI3VC4KMT_DEVICE *device);
rpi3vc4kmt_status rpi3vc4kmt_bo_create(RPI3VC4KMT_DEVICE *device,
                                       uint32_t size, RPI3VC4KMT_BO *bo);
rpi3vc4kmt_status rpi3vc4kmt_bo_create_shader(
   RPI3VC4KMT_DEVICE *device, const void *data, uint32_t size,
   RPI3VC4KMT_BO *bo);
rpi3vc4kmt_status rpi3vc4kmt_bo_map(RPI3VC4KMT_DEVICE *device,
                                    RPI3VC4KMT_BO *bo, void **cpu_va);
rpi3vc4kmt_status rpi3vc4kmt_bo_invalidate(
   RPI3VC4KMT_DEVICE *device, const RPI3VC4KMT_BO *bo,
   uint32_t offset, uint32_t length);
rpi3vc4kmt_status rpi3vc4kmt_shared_resource_info(
   RPI3VC4KMT_DEVICE *device, uint32_t global_share, void *runtime_data,
   uint32_t runtime_data_capacity, uint32_t *runtime_data_size);
rpi3vc4kmt_status rpi3vc4kmt_bo_open_shared(
   RPI3VC4KMT_DEVICE *device, uint32_t global_share, uint32_t size,
   RPI3VC4KMT_BO *bo, uint32_t *resource);
rpi3vc4kmt_status rpi3vc4kmt_bo_close_shared(
   RPI3VC4KMT_DEVICE *device, RPI3VC4KMT_BO *bo, uint32_t resource);
rpi3vc4kmt_status rpi3vc4kmt_primary_info(
   RPI3VC4KMT_DEVICE *device, uint32_t *global_share, uint32_t *width,
   uint32_t *height, uint32_t *pitch);
rpi3vc4kmt_status rpi3vc4kmt_present_primary(
   RPI3VC4KMT_DEVICE *device, const RPI3VC4KMT_BO *primary, HWND window,
   const RECT *dirty_rect);
rpi3vc4kmt_status rpi3vc4kmt_bo_destroy(RPI3VC4KMT_DEVICE *device,
                                        RPI3VC4KMT_BO *bo);
rpi3vc4kmt_status rpi3vc4kmt_submit_cl(
   RPI3VC4KMT_DEVICE *device, const RPI3VC4KMT_SUBMIT_CL *submit,
   RPI3VC4KMT_FENCE *fence);
rpi3vc4kmt_status rpi3vc4kmt_wait(RPI3VC4KMT_DEVICE *device,
                                  const RPI3VC4KMT_FENCE *fence,
                                  uint32_t timeout_ms);

_Static_assert(sizeof(RPI3VC4KMT_BO) == 24,
               "rpi3vc4kmt BO ABI mismatch");
_Static_assert(offsetof(RPI3VC4KMT_BO, cpu_va) == 8,
               "rpi3vc4kmt BO pointer offset mismatch");
_Static_assert(offsetof(RPI3VC4KMT_BO, flags) == 16,
               "rpi3vc4kmt BO flags offset mismatch");
_Static_assert(sizeof(RPI3VC4KMT_FENCE) == 24,
               "rpi3vc4kmt fence ABI mismatch");
_Static_assert(offsetof(RPI3VC4KMT_FENCE, value) == 8,
               "rpi3vc4kmt fence value offset mismatch");
_Static_assert(offsetof(RPI3VC4KMT_FENCE, cpu_value) == 16,
               "rpi3vc4kmt fence pointer offset mismatch");
_Static_assert(sizeof(RPI3VC4_SUBMIT_RCL_SURFACE) == 12,
               "rpi3vc4kmt surface ABI mismatch");
_Static_assert(sizeof(RPI3VC4KMT_SUBMIT_CL) == 160,
               "rpi3vc4kmt submit ABI mismatch");
_Static_assert(offsetof(RPI3VC4KMT_SUBMIT_CL, flags) == 156,
               "rpi3vc4kmt submit flags offset mismatch");
_Static_assert(sizeof(RPI3VC4_INFO) == 96,
               "rpi3vc4kmt info ABI mismatch");
_Static_assert(offsetof(RPI3VC4_INFO, v3d_physical) == 40,
               "rpi3vc4kmt info physical offset mismatch");

DPT_BANK vc4_present_trace;

BOOL
vc4_d3dkmt_trace_control(const void *request, void *output, ULONG bytes)
{
   LONG status = DptControl(&vc4_present_trace, request, output, bytes, GetCurrentProcessId());
   if (status < 0) {
      SetLastError((DWORD)status & 0xffff);
      return FALSE;
   }
   return TRUE;
}

#define VC4_D3DKMT_MAX_DEVICES 8
#define VC4_D3DKMT_FD_BASE 0x40000000
#define VC4_D3DKMT_MAX_SUBMIT_BOS 4096u
#define VC4_D3DKMT_INFINITE_MS 0xffffffffu
#define VC4_D3DKMT_STATUS_IO_TIMEOUT ((int32_t)0xc00000b5u)
#define VC4_D3DKMT_DWM_SURFACE_MAGIC 0x53585744u
#define VC4_D3DKMT_DWM_SURFACE_VERSION 1u

struct vc4_d3dkmt_shared_surface_info {
   uint32_t magic;
   uint32_t version;
   uint32_t width;
   uint32_t height;
   uint32_t pitch;
   uint32_t format;
};

struct vc4_d3dkmt_bo {
   bool allocated;
   RPI3VC4KMT_BO kmt;
   RPI3VC4KMT_FENCE last_fence;
   uint64_t modifier;
   uint32_t resource;
};

struct vc4_d3dkmt_device {
   RPI3VC4KMT_DEVICE *kmt;
   const RPI3VC4_INFO *info;
   mtx_t lock;
   struct vc4_d3dkmt_bo *bos;
   uint32_t bo_capacity;
   const RPI3VC4KMT_BO **submit_bos;
   uint32_t submit_bo_capacity;
   RPI3VC4KMT_FENCE timeline_fence;
   uint64_t last_seqno;
   uint32_t primary_global_share;
   uint32_t primary_width;
   uint32_t primary_height;
   uint32_t primary_pitch;
};

static once_flag registry_once = ONCE_FLAG_INIT;
static mtx_t registry_lock;
static struct vc4_d3dkmt_device *registry[VC4_D3DKMT_MAX_DEVICES];

static void
vc4_d3dkmt_registry_init(void)
{
   (void)mtx_init(&registry_lock, mtx_plain);
}

static struct vc4_d3dkmt_device *
vc4_d3dkmt_device_lookup(int fd)
{
   struct vc4_d3dkmt_device *device = NULL;

   call_once(&registry_once, vc4_d3dkmt_registry_init);
   if (fd < VC4_D3DKMT_FD_BASE ||
       fd >= VC4_D3DKMT_FD_BASE + VC4_D3DKMT_MAX_DEVICES)
      return NULL;
   mtx_lock(&registry_lock);
   device = registry[fd - VC4_D3DKMT_FD_BASE];
   mtx_unlock(&registry_lock);
   return device;
}

static bool
vc4_d3dkmt_grow_bos(struct vc4_d3dkmt_device *device, uint32_t minimum)
{
   uint32_t capacity = MAX2(device->bo_capacity, 16u);
   struct vc4_d3dkmt_bo *bos;

   while (capacity <= minimum) {
      if (capacity > UINT32_MAX / 2)
         return false;
      capacity *= 2;
   }
   if (capacity == device->bo_capacity)
      return true;
   bos = realloc(device->bos, (size_t)capacity * sizeof(*bos));
   if (!bos)
      return false;
   memset(bos + device->bo_capacity, 0,
          (size_t)(capacity - device->bo_capacity) * sizeof(*bos));
   device->bos = bos;
   device->bo_capacity = capacity;
   return true;
}

static uint32_t
vc4_d3dkmt_alloc_handle(struct vc4_d3dkmt_device *device)
{
   for (uint32_t i = 1; i < device->bo_capacity; i++) {
      if (!device->bos[i].allocated)
         return i;
   }
   uint32_t handle = device->bo_capacity;
   return vc4_d3dkmt_grow_bos(device, handle) ? handle : 0;
}

static struct vc4_d3dkmt_bo *
vc4_d3dkmt_bo_lookup_locked(struct vc4_d3dkmt_device *device,
                            uint32_t handle)
{
   if (!handle || handle >= device->bo_capacity ||
       !device->bos[handle].allocated)
      return NULL;
   return &device->bos[handle];
}

static uint32_t
vc4_d3dkmt_timeout_ms(uint64_t timeout_ns)
{
   if (timeout_ns == UINT64_MAX)
      return VC4_D3DKMT_INFINITE_MS;
   if (!timeout_ns)
      return 0;
   uint64_t timeout_ms = DIV_ROUND_UP(timeout_ns, 1000000ull);
   return timeout_ms >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)timeout_ms;
}

static int
vc4_d3dkmt_wait_fence_locked(struct vc4_d3dkmt_device *device,
                             const RPI3VC4KMT_FENCE *fence,
                             uint64_t timeout_ns)
{
   DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_BO_WAIT);
   rpi3vc4kmt_status status =
      rpi3vc4kmt_wait(device->kmt, fence,
                      vc4_d3dkmt_timeout_ms(timeout_ns));
   DptEnd(&vc4_present_trace, trace,
          status >= 0 || status == VC4_D3DKMT_STATUS_IO_TIMEOUT, 0);
   if (status >= 0)
      return 0;
   errno = status == VC4_D3DKMT_STATUS_IO_TIMEOUT ? ETIME : EIO;
   return -1;
}

static int
vc4_d3dkmt_close_bo_locked(struct vc4_d3dkmt_device *device,
                           struct vc4_d3dkmt_bo *bo)
{
   rpi3vc4kmt_status status;

   if (bo->resource)
      status = rpi3vc4kmt_bo_close_shared(device->kmt, &bo->kmt,
                                          bo->resource);
   else
      status = rpi3vc4kmt_bo_destroy(device->kmt, &bo->kmt);
   if (status < 0) {
      errno = EIO;
      return -1;
   }
   memset(bo, 0, sizeof(*bo));
   return 0;
}

static int
vc4_d3dkmt_open(void)
{
   struct vc4_d3dkmt_device *device;
   int fd = -1;

   device = calloc(1, sizeof(*device));
   if (!device)
      return -1;
   if (rpi3vc4kmt_open(&device->kmt) < 0)
      goto fail;
   device->info = rpi3vc4kmt_info(device->kmt);
   if (!device->info || !device->info->v3d_ready ||
       device->info->ident0 != 0x02443356u)
      goto fail;
   if (mtx_init(&device->lock, mtx_plain) != thrd_success)
      goto fail;
   if (!vc4_d3dkmt_grow_bos(device, 1))
      goto fail_lock;

   call_once(&registry_once, vc4_d3dkmt_registry_init);
   mtx_lock(&registry_lock);
   for (int i = 0; i < VC4_D3DKMT_MAX_DEVICES; i++) {
      if (!registry[i]) {
         registry[i] = device;
         fd = VC4_D3DKMT_FD_BASE + i;
         break;
      }
   }
   mtx_unlock(&registry_lock);
   if (fd >= VC4_D3DKMT_FD_BASE)
   {
      return fd;
   }

fail_lock:
   mtx_destroy(&device->lock);
fail:
   if (device->kmt)
      rpi3vc4kmt_close(device->kmt);
   free(device->bos);
   free(device);
   return -1;
}

void
vc4_d3dkmt_close(int fd)
{
   struct vc4_d3dkmt_device *device;

   call_once(&registry_once, vc4_d3dkmt_registry_init);
   if (fd < VC4_D3DKMT_FD_BASE ||
       fd >= VC4_D3DKMT_FD_BASE + VC4_D3DKMT_MAX_DEVICES)
      return;
   mtx_lock(&registry_lock);
   device = registry[fd - VC4_D3DKMT_FD_BASE];
   registry[fd - VC4_D3DKMT_FD_BASE] = NULL;
   mtx_unlock(&registry_lock);
   if (!device)
      return;

   {
      DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_DEVICE_LOCK);
      mtx_lock(&device->lock);
      DptEnd(&vc4_present_trace, trace, TRUE, 0);
   }
   for (uint32_t i = 1; i < device->bo_capacity; i++) {
      if (!device->bos[i].allocated)
         continue;
      if (device->bos[i].last_fence.value)
         (void)vc4_d3dkmt_wait_fence_locked(
            device, &device->bos[i].last_fence, UINT64_MAX);
      (void)vc4_d3dkmt_close_bo_locked(device, &device->bos[i]);
   }
   mtx_unlock(&device->lock);
   rpi3vc4kmt_close(device->kmt);
   mtx_destroy(&device->lock);
   free(device->submit_bos);
   free(device->bos);
   free(device);
}

void *
vc4_d3dkmt_bo_map(int fd, uint32_t handle)
{
   struct vc4_d3dkmt_device *device = vc4_d3dkmt_device_lookup(fd);
   struct vc4_d3dkmt_bo *bo;
   void *map = NULL;

   if (!device)
      return NULL;
   {
      DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_DEVICE_LOCK);
      mtx_lock(&device->lock);
      DptEnd(&vc4_present_trace, trace, TRUE, 0);
   }
   bo = vc4_d3dkmt_bo_lookup_locked(device, handle);
   if (bo && !(bo->kmt.flags & RPI3VC4KMT_BO_SHADER) &&
       rpi3vc4kmt_bo_map(device->kmt, &bo->kmt, &map) < 0)
      map = NULL;
   mtx_unlock(&device->lock);
   return map;
}

void
vc4_d3dkmt_bo_mark_cpu_dirty(int fd, uint32_t handle)
{
   struct vc4_d3dkmt_device *device = vc4_d3dkmt_device_lookup(fd);
   struct vc4_d3dkmt_bo *bo;

   if (!device)
      return;
   {
      DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_DEVICE_LOCK);
      mtx_lock(&device->lock);
      DptEnd(&vc4_present_trace, trace, TRUE, 0);
   }
   bo = vc4_d3dkmt_bo_lookup_locked(device, handle);
   if (bo)
      bo->kmt.flags |= RPI3VC4KMT_BO_CPU_DIRTY;
   mtx_unlock(&device->lock);
}

static bool
vc4_d3dkmt_primary_info_impl(int fd, uintptr_t *global_share,
                        uint32_t *width, uint32_t *height, uint32_t *pitch)
{
   struct vc4_d3dkmt_device *device = vc4_d3dkmt_device_lookup(fd);
   rpi3vc4kmt_status status;

   if (!device || !global_share || !width || !height || !pitch)
      return false;

   {
      DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_DEVICE_LOCK);
      mtx_lock(&device->lock);
      DptEnd(&vc4_present_trace, trace, TRUE, 0);
   }
   status = rpi3vc4kmt_primary_info(device->kmt,
                                    &device->primary_global_share,
                                    &device->primary_width,
                                    &device->primary_height,
                                    &device->primary_pitch);
   if (status >= 0) {
      *global_share = device->primary_global_share;
      *width = device->primary_width;
      *height = device->primary_height;
      *pitch = device->primary_pitch;
   }
   mtx_unlock(&device->lock);
   return status >= 0;
}

bool
vc4_d3dkmt_primary_info(int fd, uintptr_t *global_share,
                        uint32_t *width, uint32_t *height, uint32_t *pitch)
{
   DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_PRIMARY_QUERY);
   bool result = vc4_d3dkmt_primary_info_impl(fd, global_share, width, height, pitch);
   DptEnd(&vc4_present_trace, trace, result, 0);
   return result;
}

bool
vc4_d3dkmt_present_primary(int fd, uint32_t primary_handle, HWND window,
                           const RECT *dirty_rect)
{
   struct vc4_d3dkmt_device *device = vc4_d3dkmt_device_lookup(fd);
   struct vc4_d3dkmt_bo *primary;
   bool result = false;

   if (!device || !dirty_rect)
      return false;

   {
      DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_DEVICE_LOCK);
      mtx_lock(&device->lock);
      DptEnd(&vc4_present_trace, trace, TRUE, 0);
   }
   primary = vc4_d3dkmt_bo_lookup_locked(device, primary_handle);
   if (!primary || !primary->resource)
      goto done;
   DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_FENCE_WAIT);
   int status = primary->last_fence.value ?
      vc4_d3dkmt_wait_fence_locked(device, &primary->last_fence, UINT64_MAX) : 0;
   DptEnd(&vc4_present_trace, trace, status == 0, 0);
   if (status)
      goto done;
   trace = DptBegin(&vc4_present_trace, DPT_INVALIDATE);
   status = rpi3vc4kmt_bo_invalidate(device->kmt, &primary->kmt, 0, primary->kmt.size);
   DptEnd(&vc4_present_trace, trace, status >= 0, primary->kmt.size);
   if (status < 0)
      goto done;
   trace = DptBegin(&vc4_present_trace, DPT_KMT_PRESENT);
   result = rpi3vc4kmt_present_primary(device->kmt, &primary->kmt,
                                       window, dirty_rect) >= 0;
   DptEnd(&vc4_present_trace, trace, result, 0);

done:
   mtx_unlock(&device->lock);
   return result;
}

static int
vc4_d3dkmt_get_param(struct vc4_d3dkmt_device *device,
                     struct drm_vc4_get_param *param)
{
   if (param->pad) {
      errno = EINVAL;
      return -1;
   }
   switch (param->param) {
   case DRM_VC4_PARAM_V3D_IDENT0:
      param->value = device->info->ident0;
      break;
   case DRM_VC4_PARAM_V3D_IDENT1:
      param->value = device->info->ident1;
      break;
   case DRM_VC4_PARAM_V3D_IDENT2:
      param->value = device->info->ident2;
      break;
   case DRM_VC4_PARAM_SUPPORTS_BRANCHES:
   case DRM_VC4_PARAM_SUPPORTS_ETC1:
   case DRM_VC4_PARAM_SUPPORTS_THREADED_FS:
   case DRM_VC4_PARAM_SUPPORTS_FIXED_RCL_ORDER:
   case DRM_VC4_PARAM_SUPPORTS_MADVISE:
      param->value = 1;
      break;
   case DRM_VC4_PARAM_SUPPORTS_PERFMON:
      param->value = 0;
      break;
   default:
      errno = EINVAL;
      return -1;
   }
   return 0;
}

static void
vc4_d3dkmt_copy_surface(RPI3VC4_SUBMIT_RCL_SURFACE *destination,
                        const struct drm_vc4_submit_rcl_surface *source)
{
   destination->hindex = source->hindex;
   destination->offset = source->offset;
   destination->bits = source->bits;
   destination->flags = source->flags;
}

static bool
vc4_d3dkmt_ensure_submit_bos(struct vc4_d3dkmt_device *device,
                             uint32_t required)
{
   uint32_t capacity = MAX2(device->submit_bo_capacity, 16u);
   const RPI3VC4KMT_BO **bos;

   while (capacity < required) {
      if (capacity > UINT32_MAX / 2)
         return false;
      capacity *= 2;
   }
   if (capacity == device->submit_bo_capacity)
      return true;

   bos = realloc(device->submit_bos, (size_t)capacity * sizeof(*bos));
   if (!bos)
      return false;
   device->submit_bos = bos;
   device->submit_bo_capacity = capacity;
   return true;
}

static int
vc4_d3dkmt_submit_locked(struct vc4_d3dkmt_device *device,
                         struct drm_vc4_submit_cl *submit)
{
   const uint32_t *handles = (const uint32_t *)(uintptr_t)submit->bo_handles;
   const RPI3VC4KMT_BO **bos;
   RPI3VC4KMT_SUBMIT_CL kmt = { 0 };
   RPI3VC4KMT_FENCE fence;

   if (!submit->bo_handle_count ||
       submit->bo_handle_count > VC4_D3DKMT_MAX_SUBMIT_BOS ||
       !handles || submit->perfmonid ||
       submit->in_sync || submit->out_sync || submit->pad || submit->pad2) {
      errno = EINVAL;
      return -1;
   }
   if (!vc4_d3dkmt_ensure_submit_bos(device, submit->bo_handle_count)) {
      errno = ENOMEM;
      return -1;
   }
   bos = device->submit_bos;
   for (uint32_t i = 0; i < submit->bo_handle_count; i++) {
      struct vc4_d3dkmt_bo *bo =
         vc4_d3dkmt_bo_lookup_locked(device, handles[i]);
      if (!bo) {
         errno = EINVAL;
         return -1;
      }
      bos[i] = &bo->kmt;
   }

   kmt.bin_cl = (const void *)(uintptr_t)submit->bin_cl;
   kmt.bin_cl_size = submit->bin_cl_size;
   kmt.shader_rec = (const void *)(uintptr_t)submit->shader_rec;
   kmt.shader_rec_size = submit->shader_rec_size;
   kmt.shader_rec_count = submit->shader_rec_count;
   kmt.uniforms = (const void *)(uintptr_t)submit->uniforms;
   kmt.uniforms_size = submit->uniforms_size;
   kmt.bos = bos;
   kmt.bo_count = submit->bo_handle_count;
   kmt.width = submit->width;
   kmt.height = submit->height;
   kmt.min_x_tile = submit->min_x_tile;
   kmt.min_y_tile = submit->min_y_tile;
   kmt.max_x_tile = submit->max_x_tile;
   kmt.max_y_tile = submit->max_y_tile;
   vc4_d3dkmt_copy_surface(&kmt.color_read, &submit->color_read);
   vc4_d3dkmt_copy_surface(&kmt.color_write, &submit->color_write);
   vc4_d3dkmt_copy_surface(&kmt.zs_read, &submit->zs_read);
   vc4_d3dkmt_copy_surface(&kmt.zs_write, &submit->zs_write);
   vc4_d3dkmt_copy_surface(&kmt.msaa_color_write,
                           &submit->msaa_color_write);
   vc4_d3dkmt_copy_surface(&kmt.msaa_zs_write, &submit->msaa_zs_write);
   kmt.clear_color[0] = submit->clear_color[0];
   kmt.clear_color[1] = submit->clear_color[1];
   kmt.clear_z = submit->clear_z;
   kmt.clear_s = submit->clear_s;
   kmt.flags = submit->flags;

   DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_KMT_SUBMIT);
   rpi3vc4kmt_status status = rpi3vc4kmt_submit_cl(device->kmt, &kmt, &fence);
   DptEnd(&vc4_present_trace, trace, status >= 0, kmt.bin_cl_size);
   if (status < 0) {
      errno = EIO;
      return -1;
   }
   device->timeline_fence = fence;
   device->last_seqno = fence.value;
   submit->seqno = fence.value;
   for (uint32_t i = 0; i < submit->bo_handle_count; i++) {
      device->bos[handles[i]].last_fence = fence;
      device->bos[handles[i]].kmt.flags &= ~RPI3VC4KMT_BO_CPU_DIRTY;
   }
   return 0;
}

static int
vc4_d3dkmt_ioctl_impl(int fd, unsigned long request, void *arg)
{
   struct vc4_d3dkmt_device *device = vc4_d3dkmt_device_lookup(fd);
   int result = -1;

   if (!device || !arg) {
      errno = EINVAL;
      return -1;
   }
   {
      DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_DEVICE_LOCK);
      mtx_lock(&device->lock);
      DptEnd(&vc4_present_trace, trace, TRUE, 0);
   }
   switch (request) {
   case DRM_IOCTL_VC4_GET_PARAM:
      result = vc4_d3dkmt_get_param(device, arg);
      break;
   case DRM_IOCTL_VC4_CREATE_BO: {
      struct drm_vc4_create_bo *create = arg;
      uint32_t handle;
      struct vc4_d3dkmt_bo *bo;

      if (!create->size || create->flags || create->pad) {
         errno = EINVAL;
         break;
      }
      handle = vc4_d3dkmt_alloc_handle(device);
      if (!handle) {
         errno = ENOMEM;
         break;
      }
      bo = &device->bos[handle];
      memset(bo, 0, sizeof(*bo));
      if (rpi3vc4kmt_bo_create(device->kmt, create->size, &bo->kmt) < 0) {
         errno = ENOMEM;
         break;
      }
      bo->allocated = true;
      bo->modifier = DRM_FORMAT_MOD_LINEAR;
      create->handle = handle;
      result = 0;
      DptCount(&vc4_present_trace, DPT_BO_CREATE, create->size);
      break;
   }
   case DRM_IOCTL_VC4_CREATE_SHADER_BO: {
      struct drm_vc4_create_shader_bo *create = arg;
      uint32_t handle;
      struct vc4_d3dkmt_bo *bo;

      if (!create->size || !create->data || create->flags || create->pad) {
         errno = EINVAL;
         break;
      }
      handle = vc4_d3dkmt_alloc_handle(device);
      if (!handle) {
         errno = ENOMEM;
         break;
      }
      bo = &device->bos[handle];
      memset(bo, 0, sizeof(*bo));
      if (rpi3vc4kmt_bo_create_shader(
             device->kmt, (const void *)(uintptr_t)create->data,
             create->size, &bo->kmt) < 0) {
         errno = EINVAL;
         break;
      }
      bo->allocated = true;
      create->handle = handle;
      result = 0;
      break;
   }
   case DRM_IOCTL_VC4_MMAP_BO: {
      struct drm_vc4_mmap_bo *map = arg;
      struct vc4_d3dkmt_bo *bo =
         vc4_d3dkmt_bo_lookup_locked(device, map->handle);
      if (!bo || map->flags || (bo->kmt.flags & RPI3VC4KMT_BO_SHADER)) {
         errno = EINVAL;
         break;
      }
      map->offset = map->handle;
      result = 0;
      break;
   }
   case DRM_IOCTL_VC4_WAIT_SEQNO: {
      struct drm_vc4_wait_seqno *wait = arg;
      RPI3VC4KMT_FENCE fence = device->timeline_fence;

      if (!wait->seqno || wait->seqno > device->last_seqno ||
          !fence.sync_object) {
         errno = EINVAL;
         break;
      }
      fence.value = wait->seqno;
      result = vc4_d3dkmt_wait_fence_locked(device, &fence,
                                            wait->timeout_ns);
      break;
   }
   case DRM_IOCTL_VC4_WAIT_BO: {
      struct drm_vc4_wait_bo *wait = arg;
      struct vc4_d3dkmt_bo *bo =
         vc4_d3dkmt_bo_lookup_locked(device, wait->handle);
      if (!bo || wait->pad) {
         errno = EINVAL;
         break;
      }
      if (bo->last_fence.value &&
          vc4_d3dkmt_wait_fence_locked(device, &bo->last_fence,
                                       wait->timeout_ns))
         break;
      if (bo->kmt.cpu_va &&
          !(bo->kmt.flags & RPI3VC4KMT_BO_CPU_DIRTY) &&
          rpi3vc4kmt_bo_invalidate(device->kmt, &bo->kmt,
                                   0, bo->kmt.size) < 0) {
         errno = EIO;
         break;
      }
      result = 0;
      break;
   }
   case DRM_IOCTL_GEM_OPEN: {
      struct drm_gem_open *open = arg;
      struct vc4_d3dkmt_shared_surface_info info = { 0 };
      uint64_t size;
      uint32_t info_size = 0;
      uint32_t handle;
      struct vc4_d3dkmt_bo *bo;

      if (!open->name) {
         errno = EINVAL;
         break;
      }
      if (open->name == device->primary_global_share &&
          device->primary_width && device->primary_height &&
          device->primary_pitch >= device->primary_width * 4) {
         size = (uint64_t)device->primary_pitch * device->primary_height;
      } else {
         if (rpi3vc4kmt_shared_resource_info(
                device->kmt, open->name, &info, sizeof(info),
                &info_size) < 0 || info_size != sizeof(info) ||
             info.magic != VC4_D3DKMT_DWM_SURFACE_MAGIC ||
             info.version != VC4_D3DKMT_DWM_SURFACE_VERSION ||
             !info.width || !info.height || info.pitch < info.width * 4) {
            errno = EINVAL;
            break;
         }
         size = (uint64_t)info.pitch * info.height;
      }
      if (!size || size > UINT32_MAX) {
         errno = EOVERFLOW;
         break;
      }

      handle = vc4_d3dkmt_alloc_handle(device);
      if (!handle) {
         errno = ENOMEM;
         break;
      }
      bo = &device->bos[handle];
      memset(bo, 0, sizeof(*bo));
      if (rpi3vc4kmt_bo_open_shared(device->kmt, open->name,
                                    (uint32_t)size, &bo->kmt,
                                    &bo->resource) < 0) {
         errno = EIO;
         break;
      }
      bo->allocated = true;
      bo->modifier = DRM_FORMAT_MOD_LINEAR;
      open->handle = handle;
      open->size = size;
      result = 0;
      break;
   }
   case DRM_IOCTL_GEM_CLOSE: {
      struct drm_gem_close *close_bo = arg;
      struct vc4_d3dkmt_bo *bo =
         vc4_d3dkmt_bo_lookup_locked(device, close_bo->handle);
      int wait_result = 0;

      if (!bo || close_bo->pad) {
         errno = EINVAL;
         break;
      }
      if (bo->last_fence.value)
         wait_result = vc4_d3dkmt_wait_fence_locked(
            device, &bo->last_fence, UINT64_MAX);
      if (vc4_d3dkmt_close_bo_locked(device, bo)) {
         break;
      }
      if (wait_result) {
         errno = EIO;
         break;
      }
      result = 0;
      break;
   }
   case DRM_IOCTL_VC4_SUBMIT_CL:
      result = vc4_d3dkmt_submit_locked(device, arg);
      break;
   case DRM_IOCTL_VC4_SET_TILING: {
      struct drm_vc4_set_tiling *tiling = arg;
      struct vc4_d3dkmt_bo *bo =
         vc4_d3dkmt_bo_lookup_locked(device, tiling->handle);
      if (!bo) {
         errno = ENOENT;
         break;
      }
      if (tiling->flags ||
          (tiling->modifier != DRM_FORMAT_MOD_LINEAR &&
           tiling->modifier != DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED)) {
         errno = EINVAL;
         break;
      }
      bo->modifier = tiling->modifier;
      result = 0;
      break;
   }
   case DRM_IOCTL_VC4_GET_TILING: {
      struct drm_vc4_get_tiling *tiling = arg;
      struct vc4_d3dkmt_bo *bo =
         vc4_d3dkmt_bo_lookup_locked(device, tiling->handle);
      if (!bo) {
         errno = ENOENT;
         break;
      }
      if (tiling->flags) {
         errno = EINVAL;
         break;
      }
      tiling->modifier = bo->modifier;
      result = 0;
      break;
   }
   case DRM_IOCTL_VC4_GEM_MADVISE: {
      struct drm_vc4_gem_madvise *madvise = arg;
      if (!vc4_d3dkmt_bo_lookup_locked(device, madvise->handle) ||
          madvise->pad || madvise->madv > VC4_MADV_DONTNEED) {
         errno = EINVAL;
         break;
      }
      madvise->retained = 1;
      result = 0;
      break;
   }
   case DRM_IOCTL_VC4_LABEL_BO:
      result = 0;
      break;
   default:
      errno = EOPNOTSUPP;
      result = -1;
      break;
   }
   mtx_unlock(&device->lock);
   return result;
}

struct pipe_screen *
vc4_d3dkmt_screen_create(const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;
   int fd;

   fd = vc4_d3dkmt_open();

   if (fd < 0)
      return NULL;
   screen = vc4_screen_create(fd, config, NULL);
   if (!screen)
      vc4_d3dkmt_close(fd);
   return screen;
}

int
vc4_d3dkmt_ioctl(int fd, unsigned long request, void *arg)
{
   DPT_SCOPE trace = DptBegin(&vc4_present_trace, DPT_IOCTL);
   int result = vc4_d3dkmt_ioctl_impl(fd, request, arg);
   DptEnd(&vc4_present_trace, trace, result >= 0, 0);
   return result;
}
