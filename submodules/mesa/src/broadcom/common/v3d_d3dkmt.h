/*
 * Copyright 2026 ReactOS Team
 * SPDX-License-Identifier: MIT
 *
 * Minimal libdrm-shaped interface used by the V3D Gallium driver on ReactOS.
 */

#ifndef V3D_D3DKMT_H
#define V3D_D3DKMT_H

#include <stdint.h>

#include "drm-uapi/drm.h"
#include "drm-uapi/v3d_drm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V3D_D3DKMT_SUBMIT_HANDLE_WRITE (UINT32_C(1) << 31)
#define V3D_D3DKMT_SUBMIT_HANDLE_MASK  (~V3D_D3DKMT_SUBMIT_HANDLE_WRITE)

int
drmIoctl(int fd, unsigned long request, void *arg);

int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);

int
drmSyncobjDestroy(int fd, uint32_t handle);

int
drmSyncobjWait(int fd, const uint32_t *handles, unsigned num_handles,
               int64_t timeout_nsec, unsigned flags,
               uint32_t *first_signaled);

int
v3d_d3dkmt_syncobj_signal_event(int fd, uint32_t handle, void *event);

int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);

int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);

int
drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);

int
v3d_d3dkmt_open(void);

void
v3d_d3dkmt_close(int fd);

void *
v3d_d3dkmt_bo_map(int fd, uint32_t handle);

int
v3d_d3dkmt_bo_prepare_cpu_access(int fd, uint32_t handle, int write);

int
v3d_d3dkmt_bo_mark_external_dirty(int fd, uint32_t handle);

int
v3d_d3dkmt_syncobj_clone(int fd, uint32_t source, uint32_t *destination);

int
v3d_d3dkmt_present_linear(int fd, uintptr_t window, uint32_t source_handle,
                          uint32_t out_sync, uint32_t source_offset,
                          uint32_t source_stride,
                          uint32_t source_padded_height,
                          uint32_t source_tiling, uint32_t source_size,
                          uint32_t destination_x, uint32_t destination_y,
                          uint32_t width, uint32_t height,
                          uint32_t screen_width, uint32_t screen_height);

#ifdef __cplusplus
}
#endif

#endif /* V3D_D3DKMT_H */
