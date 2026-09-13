/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include <sys/mman.h>

#include "DrmVirtGpu.h"
#include "drm-uapi/virtgpu_drm.h"

DrmVirtGpuResourceMapping::DrmVirtGpuResourceMapping(VirtGpuResourcePtr blob, uint8_t* ptr,
                                                         uint64_t size)
    : mBlob(blob), mPtr(ptr), mSize(size) {}

DrmVirtGpuResourceMapping::~DrmVirtGpuResourceMapping(void) { munmap(mPtr, mSize); }

uint8_t* DrmVirtGpuResourceMapping::asRawPtr(void) { return mPtr; }
