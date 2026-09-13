/*
 * Copyright 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef INTEL_VIRTIO_H_
#define INTEL_VIRTIO_H_

#include <stdbool.h>
#include <sys/mman.h>

#include "util/macros.h"

struct intel_device_info;

#ifdef HAVE_INTEL_VIRTIO
int intel_virtio_init_fd(int fd);
void intel_virtio_ref_fd(int fd);
void intel_virtio_unref_fd(int fd);
bool intel_virtio_get_pci_device_info(int fd,
                                      struct intel_device_info *devinfo);
bool is_intel_virtio_fd(int fd);
void *intel_virtio_bo_mmap(int fd, uint32_t handle, size_t size, void *placed_addr);
struct util_sync_provider *intel_virtio_sync_provider(int fd);
#else
static inline int intel_virtio_init_fd(int fd)
{
   return 0;
}

static inline void intel_virtio_ref_fd(int fd) {};

static inline void intel_virtio_unref_fd(int fd) {};

static inline bool
intel_virtio_get_pci_device_info(int fd, struct intel_device_info *devinfo)
{
   return false;
}

static inline bool is_intel_virtio_fd(int fd)
{
   return false;
}

static inline void *
intel_virtio_bo_mmap(int fd, uint32_t handle, size_t size, void *placed_addr)
{
   return MAP_FAILED;
}

static struct util_sync_provider *
intel_virtio_sync_provider(int fd)
{
   return NULL;
}
#endif /* HAVE_INTEL_VIRTIO */

#endif /* INTEL_VIRTIO_H_ */
