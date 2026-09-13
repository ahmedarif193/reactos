/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "GfxStreamVulkanConnection.h"

#include <string>

#include "util/detect_os.h"
#include "util/u_process.h"
#include "VirtGpu.h"

#if DETECT_OS_LINUX
#include <sys/prctl.h>
#if !defined(HAVE_GETTID)
#include <sys/syscall.h>
#endif  // !defined(HAVE_GETTID)
#include <unistd.h>
#endif

namespace {

// TODO: move to a more common src/util location.
static uint64_t getProcessId() {
#if DETECT_OS_LINUX
    return getpid();
#endif
    return -1;
}

// TODO: move to a more common src/util location.
static std::string getThreadName() {
    std::string threadName;
#if DETECT_OS_LINUX
    char buf[16] = {};
    if (prctl(PR_GET_NAME, buf) == 0) {
        threadName = buf;
    }
#endif
    return threadName;
}

// TODO: move to a more common src/util location.
static uint64_t getThreadId() {
#if DETECT_OS_LINUX
#if defined(HAVE_GETTID)
    return gettid();
#else
    return (pid_t)syscall(SYS_gettid);
#endif  // defined(HAVE_GETTID)
#endif
    return -1;
}

}  // namespace

GfxStreamVulkanConnection::GfxStreamVulkanConnection(gfxstream::guest::IOStream* stream) {
    mVkEnc = std::make_unique<gfxstream::vk::VkEncoder>(stream);

    auto* device = VirtGpuDevice::getInstance(kCapsetGfxStreamVulkan);
    if (device != nullptr) {
        const auto caps = device->getCaps();
        if (caps.vulkanCapset.hasSetMetadataCommand) {
            const std::string processName = util_get_process_name();
            const struct VkDebugMetadataGuestProcessNameGOOGLE debugMetadataGuestProcessName = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_METADATA_GUEST_PROCESS_NAME_GOOGLE,
                .pNext = nullptr,
                .pName = processName.c_str(),
            };
            const uint64_t processId = getProcessId();
            const struct VkDebugMetadataGuestThreadIdGOOGLE debugMetadataGuestProcessId = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_METADATA_GUEST_PROCESS_ID_GOOGLE,
                .pNext = &debugMetadataGuestProcessName,
                .id = processId,
            };
            const std::string threadName = getThreadName();
            const struct VkDebugMetadataGuestThreadNameGOOGLE debugMetadataGuestThreadName = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_METADATA_GUEST_THREAD_NAME_GOOGLE,
                .pNext = &debugMetadataGuestProcessId,
                .pName = threadName.c_str(),
            };
            const uint64_t threadId = getThreadId();
            const struct VkDebugMetadataGuestThreadIdGOOGLE debugMetadataGuestThreadId = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_METADATA_GUEST_THREAD_ID_GOOGLE,
                .pNext = &debugMetadataGuestThreadName,
                .id = threadId,
            };
            const struct VkDebugMetadataGOOGLE debugMetadata = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_METADATA_GOOGLE,
                .pNext = &debugMetadataGuestThreadId,
            };
            mVkEnc->vkSetDebugMetadataAsyncGOOGLE(&debugMetadata, false /* no lock */);
        }
    }
}

GfxStreamVulkanConnection::~GfxStreamVulkanConnection() {}

void* GfxStreamVulkanConnection::getEncoder() { return mVkEnc.get(); }
