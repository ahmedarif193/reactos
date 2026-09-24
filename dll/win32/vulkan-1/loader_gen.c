#include "vulkan_private.h"

const char *const vk_instance_function_names[VKI_COUNT] =
{
    "vkCreateDisplayModeKHR",
    "vkCreateDisplayPlaneSurfaceKHR",
    "vkCreateWin32SurfaceKHR",
    "vkDestroySurfaceKHR",
    "vkGetDisplayModePropertiesKHR",
    "vkGetDisplayPlaneCapabilitiesKHR",
    "vkGetDisplayPlaneSupportedDisplaysKHR",
    "vkGetPhysicalDeviceDisplayPlanePropertiesKHR",
    "vkGetPhysicalDeviceDisplayPropertiesKHR",
    "vkGetPhysicalDeviceExternalBufferProperties",
    "vkGetPhysicalDeviceExternalFenceProperties",
    "vkGetPhysicalDeviceExternalSemaphoreProperties",
    "vkGetPhysicalDeviceFeatures",
    "vkGetPhysicalDeviceFeatures2",
    "vkGetPhysicalDeviceFormatProperties",
    "vkGetPhysicalDeviceFormatProperties2",
    "vkGetPhysicalDeviceImageFormatProperties",
    "vkGetPhysicalDeviceImageFormatProperties2",
    "vkGetPhysicalDeviceMemoryProperties",
    "vkGetPhysicalDeviceMemoryProperties2",
    "vkGetPhysicalDevicePresentRectanglesKHR",
    "vkGetPhysicalDeviceProperties",
    "vkGetPhysicalDeviceProperties2",
    "vkGetPhysicalDeviceQueueFamilyProperties",
    "vkGetPhysicalDeviceQueueFamilyProperties2",
    "vkGetPhysicalDeviceSparseImageFormatProperties",
    "vkGetPhysicalDeviceSparseImageFormatProperties2",
    "vkGetPhysicalDeviceSurfaceCapabilities2KHR",
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
    "vkGetPhysicalDeviceSurfaceFormats2KHR",
    "vkGetPhysicalDeviceSurfaceFormatsKHR",
    "vkGetPhysicalDeviceSurfacePresentModesKHR",
    "vkGetPhysicalDeviceSurfaceSupportKHR",
    "vkGetPhysicalDeviceToolProperties",
    "vkGetPhysicalDeviceWin32PresentationSupportKHR",
    "vkGetDeviceProcAddr",
    "vkCreateDevice",
    "vkEnumeratePhysicalDevices",
    "vkEnumeratePhysicalDeviceGroups",
    "vkEnumerateDeviceExtensionProperties",
    "vkDestroyInstance",
};

const char *const vk_device_function_names[VKD_COUNT] =
{
    "vkAcquireNextImage2KHR",
    "vkAcquireNextImageKHR",
    "vkAllocateDescriptorSets",
    "vkAllocateMemory",
    "vkBeginCommandBuffer",
    "vkBindBufferMemory",
    "vkBindBufferMemory2",
    "vkBindImageMemory",
    "vkBindImageMemory2",
    "vkCmdBeginQuery",
    "vkCmdBeginRenderPass",
    "vkCmdBeginRenderPass2",
    "vkCmdBeginRendering",
    "vkCmdBindDescriptorSets",
    "vkCmdBindDescriptorSets2",
    "vkCmdBindIndexBuffer",
    "vkCmdBindIndexBuffer2",
    "vkCmdBindPipeline",
    "vkCmdBindVertexBuffers",
    "vkCmdBindVertexBuffers2",
    "vkCmdBlitImage",
    "vkCmdBlitImage2",
    "vkCmdClearAttachments",
    "vkCmdClearColorImage",
    "vkCmdClearDepthStencilImage",
    "vkCmdCopyBuffer",
    "vkCmdCopyBuffer2",
    "vkCmdCopyBufferToImage",
    "vkCmdCopyBufferToImage2",
    "vkCmdCopyImage",
    "vkCmdCopyImage2",
    "vkCmdCopyImageToBuffer",
    "vkCmdCopyImageToBuffer2",
    "vkCmdCopyQueryPoolResults",
    "vkCmdDispatch",
    "vkCmdDispatchBase",
    "vkCmdDispatchIndirect",
    "vkCmdDraw",
    "vkCmdDrawIndexed",
    "vkCmdDrawIndexedIndirect",
    "vkCmdDrawIndexedIndirectCount",
    "vkCmdDrawIndirect",
    "vkCmdDrawIndirectCount",
    "vkCmdEndQuery",
    "vkCmdEndRenderPass",
    "vkCmdEndRenderPass2",
    "vkCmdEndRendering",
    "vkCmdExecuteCommands",
    "vkCmdFillBuffer",
    "vkCmdNextSubpass",
    "vkCmdNextSubpass2",
    "vkCmdPipelineBarrier",
    "vkCmdPipelineBarrier2",
    "vkCmdPushConstants",
    "vkCmdPushConstants2",
    "vkCmdPushDescriptorSet",
    "vkCmdPushDescriptorSet2",
    "vkCmdPushDescriptorSetWithTemplate",
    "vkCmdPushDescriptorSetWithTemplate2",
    "vkCmdResetEvent",
    "vkCmdResetEvent2",
    "vkCmdResetQueryPool",
    "vkCmdResolveImage",
    "vkCmdResolveImage2",
    "vkCmdSetBlendConstants",
    "vkCmdSetCullMode",
    "vkCmdSetDepthBias",
    "vkCmdSetDepthBiasEnable",
    "vkCmdSetDepthBounds",
    "vkCmdSetDepthBoundsTestEnable",
    "vkCmdSetDepthCompareOp",
    "vkCmdSetDepthTestEnable",
    "vkCmdSetDepthWriteEnable",
    "vkCmdSetDeviceMask",
    "vkCmdSetEvent",
    "vkCmdSetEvent2",
    "vkCmdSetFrontFace",
    "vkCmdSetLineStipple",
    "vkCmdSetLineWidth",
    "vkCmdSetPrimitiveRestartEnable",
    "vkCmdSetPrimitiveTopology",
    "vkCmdSetRasterizerDiscardEnable",
    "vkCmdSetRenderingAttachmentLocations",
    "vkCmdSetRenderingInputAttachmentIndices",
    "vkCmdSetScissor",
    "vkCmdSetScissorWithCount",
    "vkCmdSetStencilCompareMask",
    "vkCmdSetStencilOp",
    "vkCmdSetStencilReference",
    "vkCmdSetStencilTestEnable",
    "vkCmdSetStencilWriteMask",
    "vkCmdSetViewport",
    "vkCmdSetViewportWithCount",
    "vkCmdUpdateBuffer",
    "vkCmdWaitEvents",
    "vkCmdWaitEvents2",
    "vkCmdWriteTimestamp",
    "vkCmdWriteTimestamp2",
    "vkCopyImageToImage",
    "vkCopyImageToMemory",
    "vkCopyMemoryToImage",
    "vkCreateBuffer",
    "vkCreateBufferView",
    "vkCreateCommandPool",
    "vkCreateComputePipelines",
    "vkCreateDescriptorPool",
    "vkCreateDescriptorSetLayout",
    "vkCreateDescriptorUpdateTemplate",
    "vkCreateEvent",
    "vkCreateFence",
    "vkCreateFramebuffer",
    "vkCreateGraphicsPipelines",
    "vkCreateImage",
    "vkCreateImageView",
    "vkCreatePipelineCache",
    "vkCreatePipelineLayout",
    "vkCreatePrivateDataSlot",
    "vkCreateQueryPool",
    "vkCreateRenderPass",
    "vkCreateRenderPass2",
    "vkCreateSampler",
    "vkCreateSamplerYcbcrConversion",
    "vkCreateSemaphore",
    "vkCreateShaderModule",
    "vkCreateSharedSwapchainsKHR",
    "vkCreateSwapchainKHR",
    "vkDestroyBuffer",
    "vkDestroyBufferView",
    "vkDestroyCommandPool",
    "vkDestroyDescriptorPool",
    "vkDestroyDescriptorSetLayout",
    "vkDestroyDescriptorUpdateTemplate",
    "vkDestroyEvent",
    "vkDestroyFence",
    "vkDestroyFramebuffer",
    "vkDestroyImage",
    "vkDestroyImageView",
    "vkDestroyPipeline",
    "vkDestroyPipelineCache",
    "vkDestroyPipelineLayout",
    "vkDestroyPrivateDataSlot",
    "vkDestroyQueryPool",
    "vkDestroyRenderPass",
    "vkDestroySampler",
    "vkDestroySamplerYcbcrConversion",
    "vkDestroySemaphore",
    "vkDestroyShaderModule",
    "vkDestroySwapchainKHR",
    "vkDeviceWaitIdle",
    "vkEndCommandBuffer",
    "vkFlushMappedMemoryRanges",
    "vkFreeCommandBuffers",
    "vkFreeDescriptorSets",
    "vkFreeMemory",
    "vkGetBufferDeviceAddress",
    "vkGetBufferMemoryRequirements",
    "vkGetBufferMemoryRequirements2",
    "vkGetBufferOpaqueCaptureAddress",
    "vkGetDescriptorSetLayoutSupport",
    "vkGetDeviceBufferMemoryRequirements",
    "vkGetDeviceGroupPeerMemoryFeatures",
    "vkGetDeviceGroupPresentCapabilitiesKHR",
    "vkGetDeviceGroupSurfacePresentModesKHR",
    "vkGetDeviceImageMemoryRequirements",
    "vkGetDeviceImageSparseMemoryRequirements",
    "vkGetDeviceImageSubresourceLayout",
    "vkGetDeviceMemoryCommitment",
    "vkGetDeviceMemoryOpaqueCaptureAddress",
    "vkGetEventStatus",
    "vkGetFenceStatus",
    "vkGetImageMemoryRequirements",
    "vkGetImageMemoryRequirements2",
    "vkGetImageSparseMemoryRequirements",
    "vkGetImageSparseMemoryRequirements2",
    "vkGetImageSubresourceLayout",
    "vkGetImageSubresourceLayout2",
    "vkGetPipelineCacheData",
    "vkGetPrivateData",
    "vkGetQueryPoolResults",
    "vkGetRenderAreaGranularity",
    "vkGetRenderingAreaGranularity",
    "vkGetSemaphoreCounterValue",
    "vkGetSwapchainImagesKHR",
    "vkInvalidateMappedMemoryRanges",
    "vkMapMemory",
    "vkMapMemory2",
    "vkMergePipelineCaches",
    "vkQueueBindSparse",
    "vkQueuePresentKHR",
    "vkQueueSubmit",
    "vkQueueSubmit2",
    "vkQueueWaitIdle",
    "vkResetCommandBuffer",
    "vkResetCommandPool",
    "vkResetDescriptorPool",
    "vkResetEvent",
    "vkResetFences",
    "vkResetQueryPool",
    "vkSetEvent",
    "vkSetPrivateData",
    "vkSignalSemaphore",
    "vkTransitionImageLayout",
    "vkTrimCommandPool",
    "vkUnmapMemory",
    "vkUnmapMemory2",
    "vkUpdateDescriptorSetWithTemplate",
    "vkUpdateDescriptorSets",
    "vkWaitForFences",
    "vkWaitSemaphores",
    "vkDestroyDevice",
    "vkGetDeviceQueue",
    "vkGetDeviceQueue2",
    "vkAllocateCommandBuffers",
};

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode)
{
    return ((PFN_vkCreateDisplayModeKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkCreateDisplayModeKHR])(physicalDevice, display, pCreateInfo, pAllocator, pMode);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    return ((PFN_vkCreateDisplayPlaneSurfaceKHR)vk_instance_table(instance)->Functions[VKI_vkCreateDisplayPlaneSurfaceKHR])(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    return ((PFN_vkCreateWin32SurfaceKHR)vk_instance_table(instance)->Functions[VKI_vkCreateWin32SurfaceKHR])(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroySurfaceKHR)vk_instance_table(instance)->Functions[VKI_vkDestroySurfaceKHR])(instance, surface, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties)
{
    return ((PFN_vkGetDisplayModePropertiesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetDisplayModePropertiesKHR])(physicalDevice, display, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities)
{
    return ((PFN_vkGetDisplayPlaneCapabilitiesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetDisplayPlaneCapabilitiesKHR])(physicalDevice, mode, planeIndex, pCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays)
{
    return ((PFN_vkGetDisplayPlaneSupportedDisplaysKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetDisplayPlaneSupportedDisplaysKHR])(physicalDevice, planeIndex, pDisplayCount, pDisplays);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties)
{
    return ((PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceDisplayPlanePropertiesKHR])(physicalDevice, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties)
{
    return ((PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceDisplayPropertiesKHR])(physicalDevice, pPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties)
{
    ((PFN_vkGetPhysicalDeviceExternalBufferProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceExternalBufferProperties])(physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties)
{
    ((PFN_vkGetPhysicalDeviceExternalFenceProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceExternalFenceProperties])(physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties)
{
    ((PFN_vkGetPhysicalDeviceExternalSemaphoreProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceExternalSemaphoreProperties])(physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures)
{
    ((PFN_vkGetPhysicalDeviceFeatures)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceFeatures])(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{
    ((PFN_vkGetPhysicalDeviceFeatures2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceFeatures2])(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
{
    ((PFN_vkGetPhysicalDeviceFormatProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceFormatProperties])(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties)
{
    ((PFN_vkGetPhysicalDeviceFormatProperties2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceFormatProperties2])(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties)
{
    return ((PFN_vkGetPhysicalDeviceImageFormatProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceImageFormatProperties])(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties)
{
    return ((PFN_vkGetPhysicalDeviceImageFormatProperties2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceImageFormatProperties2])(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{
    ((PFN_vkGetPhysicalDeviceMemoryProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceMemoryProperties])(physicalDevice, pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
{
    ((PFN_vkGetPhysicalDeviceMemoryProperties2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceMemoryProperties2])(physicalDevice, pMemoryProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects)
{
    return ((PFN_vkGetPhysicalDevicePresentRectanglesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDevicePresentRectanglesKHR])(physicalDevice, surface, pRectCount, pRects);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties)
{
    ((PFN_vkGetPhysicalDeviceProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceProperties])(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties)
{
    ((PFN_vkGetPhysicalDeviceProperties2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceProperties2])(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties)
{
    ((PFN_vkGetPhysicalDeviceQueueFamilyProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceQueueFamilyProperties])(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties)
{
    ((PFN_vkGetPhysicalDeviceQueueFamilyProperties2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceQueueFamilyProperties2])(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties)
{
    ((PFN_vkGetPhysicalDeviceSparseImageFormatProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSparseImageFormatProperties])(physicalDevice, format, type, samples, usage, tiling, pPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties)
{
    ((PFN_vkGetPhysicalDeviceSparseImageFormatProperties2)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSparseImageFormatProperties2])(physicalDevice, pFormatInfo, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities)
{
    return ((PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSurfaceCapabilities2KHR])(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    return ((PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSurfaceCapabilitiesKHR])(physicalDevice, surface, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats)
{
    return ((PFN_vkGetPhysicalDeviceSurfaceFormats2KHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSurfaceFormats2KHR])(physicalDevice, pSurfaceInfo, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
    return ((PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSurfaceFormatsKHR])(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{
    return ((PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSurfacePresentModesKHR])(physicalDevice, surface, pPresentModeCount, pPresentModes);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported)
{
    return ((PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceSurfaceSupportKHR])(physicalDevice, queueFamilyIndex, surface, pSupported);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t* pToolCount, VkPhysicalDeviceToolProperties* pToolProperties)
{
    return ((PFN_vkGetPhysicalDeviceToolProperties)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceToolProperties])(physicalDevice, pToolCount, pToolProperties);
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex)
{
    return ((PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)vk_instance_table(physicalDevice)->Functions[VKI_vkGetPhysicalDeviceWin32PresentationSupportKHR])(physicalDevice, queueFamilyIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex)
{
    return ((PFN_vkAcquireNextImage2KHR)vk_device_table(device)->Functions[VKD_vkAcquireNextImage2KHR])(device, pAcquireInfo, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    return ((PFN_vkAcquireNextImageKHR)vk_device_table(device)->Functions[VKD_vkAcquireNextImageKHR])(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
{
    return ((PFN_vkAllocateDescriptorSets)vk_device_table(device)->Functions[VKD_vkAllocateDescriptorSets])(device, pAllocateInfo, pDescriptorSets);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
    return ((PFN_vkAllocateMemory)vk_device_table(device)->Functions[VKD_vkAllocateMemory])(device, pAllocateInfo, pAllocator, pMemory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{
    return ((PFN_vkBeginCommandBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkBeginCommandBuffer])(commandBuffer, pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    return ((PFN_vkBindBufferMemory)vk_device_table(device)->Functions[VKD_vkBindBufferMemory])(device, buffer, memory, memoryOffset);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos)
{
    return ((PFN_vkBindBufferMemory2)vk_device_table(device)->Functions[VKD_vkBindBufferMemory2])(device, bindInfoCount, pBindInfos);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    return ((PFN_vkBindImageMemory)vk_device_table(device)->Functions[VKD_vkBindImageMemory])(device, image, memory, memoryOffset);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos)
{
    return ((PFN_vkBindImageMemory2)vk_device_table(device)->Functions[VKD_vkBindImageMemory2])(device, bindInfoCount, pBindInfos);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
{
    ((PFN_vkCmdBeginQuery)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBeginQuery])(commandBuffer, queryPool, query, flags);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents)
{
    ((PFN_vkCmdBeginRenderPass)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBeginRenderPass])(commandBuffer, pRenderPassBegin, contents);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo)
{
    ((PFN_vkCmdBeginRenderPass2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBeginRenderPass2])(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{
    ((PFN_vkCmdBeginRendering)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBeginRendering])(commandBuffer, pRenderingInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
    ((PFN_vkCmdBindDescriptorSets)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindDescriptorSets])(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo)
{
    ((PFN_vkCmdBindDescriptorSets2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindDescriptorSets2])(commandBuffer, pBindDescriptorSetsInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
    ((PFN_vkCmdBindIndexBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindIndexBuffer])(commandBuffer, buffer, offset, indexType);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType)
{
    ((PFN_vkCmdBindIndexBuffer2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindIndexBuffer2])(commandBuffer, buffer, offset, size, indexType);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    ((PFN_vkCmdBindPipeline)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindPipeline])(commandBuffer, pipelineBindPoint, pipeline);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets)
{
    ((PFN_vkCmdBindVertexBuffers)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindVertexBuffers])(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides)
{
    ((PFN_vkCmdBindVertexBuffers2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBindVertexBuffers2])(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{
    ((PFN_vkCmdBlitImage)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBlitImage])(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo)
{
    ((PFN_vkCmdBlitImage2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdBlitImage2])(commandBuffer, pBlitImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects)
{
    ((PFN_vkCmdClearAttachments)vk_device_table(commandBuffer)->Functions[VKD_vkCmdClearAttachments])(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
{
    ((PFN_vkCmdClearColorImage)vk_device_table(commandBuffer)->Functions[VKD_vkCmdClearColorImage])(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
{
    ((PFN_vkCmdClearDepthStencilImage)vk_device_table(commandBuffer)->Functions[VKD_vkCmdClearDepthStencilImage])(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
{
    ((PFN_vkCmdCopyBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyBuffer])(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo)
{
    ((PFN_vkCmdCopyBuffer2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyBuffer2])(commandBuffer, pCopyBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    ((PFN_vkCmdCopyBufferToImage)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyBufferToImage])(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
    ((PFN_vkCmdCopyBufferToImage2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyBufferToImage2])(commandBuffer, pCopyBufferToImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions)
{
    ((PFN_vkCmdCopyImage)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyImage])(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo)
{
    ((PFN_vkCmdCopyImage2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyImage2])(commandBuffer, pCopyImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    ((PFN_vkCmdCopyImageToBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyImageToBuffer])(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo)
{
    ((PFN_vkCmdCopyImageToBuffer2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyImageToBuffer2])(commandBuffer, pCopyImageToBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
{
    ((PFN_vkCmdCopyQueryPoolResults)vk_device_table(commandBuffer)->Functions[VKD_vkCmdCopyQueryPoolResults])(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    ((PFN_vkCmdDispatch)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDispatch])(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    ((PFN_vkCmdDispatchBase)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDispatchBase])(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    ((PFN_vkCmdDispatchIndirect)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDispatchIndirect])(commandBuffer, buffer, offset);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    ((PFN_vkCmdDraw)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDraw])(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    ((PFN_vkCmdDrawIndexed)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDrawIndexed])(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    ((PFN_vkCmdDrawIndexedIndirect)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDrawIndexedIndirect])(commandBuffer, buffer, offset, drawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
    ((PFN_vkCmdDrawIndexedIndirectCount)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDrawIndexedIndirectCount])(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    ((PFN_vkCmdDrawIndirect)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDrawIndirect])(commandBuffer, buffer, offset, drawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
    ((PFN_vkCmdDrawIndirectCount)vk_device_table(commandBuffer)->Functions[VKD_vkCmdDrawIndirectCount])(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
    ((PFN_vkCmdEndQuery)vk_device_table(commandBuffer)->Functions[VKD_vkCmdEndQuery])(commandBuffer, queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    ((PFN_vkCmdEndRenderPass)vk_device_table(commandBuffer)->Functions[VKD_vkCmdEndRenderPass])(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo)
{
    ((PFN_vkCmdEndRenderPass2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdEndRenderPass2])(commandBuffer, pSubpassEndInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    ((PFN_vkCmdEndRendering)vk_device_table(commandBuffer)->Functions[VKD_vkCmdEndRendering])(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
{
    ((PFN_vkCmdExecuteCommands)vk_device_table(commandBuffer)->Functions[VKD_vkCmdExecuteCommands])(commandBuffer, commandBufferCount, pCommandBuffers);
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{
    ((PFN_vkCmdFillBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkCmdFillBuffer])(commandBuffer, dstBuffer, dstOffset, size, data);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
    ((PFN_vkCmdNextSubpass)vk_device_table(commandBuffer)->Functions[VKD_vkCmdNextSubpass])(commandBuffer, contents);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo)
{
    ((PFN_vkCmdNextSubpass2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdNextSubpass2])(commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    ((PFN_vkCmdPipelineBarrier)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPipelineBarrier])(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo)
{
    ((PFN_vkCmdPipelineBarrier2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPipelineBarrier2])(commandBuffer, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
{
    ((PFN_vkCmdPushConstants)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPushConstants])(commandBuffer, layout, stageFlags, offset, size, pValues);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo)
{
    ((PFN_vkCmdPushConstants2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPushConstants2])(commandBuffer, pPushConstantsInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
{
    ((PFN_vkCmdPushDescriptorSet)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPushDescriptorSet])(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo* pPushDescriptorSetInfo)
{
    ((PFN_vkCmdPushDescriptorSet2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPushDescriptorSet2])(commandBuffer, pPushDescriptorSetInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData)
{
    ((PFN_vkCmdPushDescriptorSetWithTemplate)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPushDescriptorSetWithTemplate])(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo)
{
    ((PFN_vkCmdPushDescriptorSetWithTemplate2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdPushDescriptorSetWithTemplate2])(commandBuffer, pPushDescriptorSetWithTemplateInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    ((PFN_vkCmdResetEvent)vk_device_table(commandBuffer)->Functions[VKD_vkCmdResetEvent])(commandBuffer, event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
    ((PFN_vkCmdResetEvent2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdResetEvent2])(commandBuffer, event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    ((PFN_vkCmdResetQueryPool)vk_device_table(commandBuffer)->Functions[VKD_vkCmdResetQueryPool])(commandBuffer, queryPool, firstQuery, queryCount);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions)
{
    ((PFN_vkCmdResolveImage)vk_device_table(commandBuffer)->Functions[VKD_vkCmdResolveImage])(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo)
{
    ((PFN_vkCmdResolveImage2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdResolveImage2])(commandBuffer, pResolveImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
{
    ((PFN_vkCmdSetBlendConstants)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetBlendConstants])(commandBuffer, blendConstants);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode)
{
    ((PFN_vkCmdSetCullMode)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetCullMode])(commandBuffer, cullMode);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
    ((PFN_vkCmdSetDepthBias)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthBias])(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable)
{
    ((PFN_vkCmdSetDepthBiasEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthBiasEnable])(commandBuffer, depthBiasEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds)
{
    ((PFN_vkCmdSetDepthBounds)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthBounds])(commandBuffer, minDepthBounds, maxDepthBounds);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable)
{
    ((PFN_vkCmdSetDepthBoundsTestEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthBoundsTestEnable])(commandBuffer, depthBoundsTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp)
{
    ((PFN_vkCmdSetDepthCompareOp)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthCompareOp])(commandBuffer, depthCompareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable)
{
    ((PFN_vkCmdSetDepthTestEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthTestEnable])(commandBuffer, depthTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable)
{
    ((PFN_vkCmdSetDepthWriteEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDepthWriteEnable])(commandBuffer, depthWriteEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
    ((PFN_vkCmdSetDeviceMask)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetDeviceMask])(commandBuffer, deviceMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    ((PFN_vkCmdSetEvent)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetEvent])(commandBuffer, event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo)
{
    ((PFN_vkCmdSetEvent2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetEvent2])(commandBuffer, event, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
    ((PFN_vkCmdSetFrontFace)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetFrontFace])(commandBuffer, frontFace);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern)
{
    ((PFN_vkCmdSetLineStipple)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetLineStipple])(commandBuffer, lineStippleFactor, lineStipplePattern);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
    ((PFN_vkCmdSetLineWidth)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetLineWidth])(commandBuffer, lineWidth);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable)
{
    ((PFN_vkCmdSetPrimitiveRestartEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetPrimitiveRestartEnable])(commandBuffer, primitiveRestartEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology)
{
    ((PFN_vkCmdSetPrimitiveTopology)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetPrimitiveTopology])(commandBuffer, primitiveTopology);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable)
{
    ((PFN_vkCmdSetRasterizerDiscardEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetRasterizerDiscardEnable])(commandBuffer, rasterizerDiscardEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo* pLocationInfo)
{
    ((PFN_vkCmdSetRenderingAttachmentLocations)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetRenderingAttachmentLocations])(commandBuffer, pLocationInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo)
{
    ((PFN_vkCmdSetRenderingInputAttachmentIndices)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetRenderingInputAttachmentIndices])(commandBuffer, pInputAttachmentIndexInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors)
{
    ((PFN_vkCmdSetScissor)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetScissor])(commandBuffer, firstScissor, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D* pScissors)
{
    ((PFN_vkCmdSetScissorWithCount)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetScissorWithCount])(commandBuffer, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask)
{
    ((PFN_vkCmdSetStencilCompareMask)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetStencilCompareMask])(commandBuffer, faceMask, compareMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp)
{
    ((PFN_vkCmdSetStencilOp)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetStencilOp])(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference)
{
    ((PFN_vkCmdSetStencilReference)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetStencilReference])(commandBuffer, faceMask, reference);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable)
{
    ((PFN_vkCmdSetStencilTestEnable)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetStencilTestEnable])(commandBuffer, stencilTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask)
{
    ((PFN_vkCmdSetStencilWriteMask)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetStencilWriteMask])(commandBuffer, faceMask, writeMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports)
{
    ((PFN_vkCmdSetViewport)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetViewport])(commandBuffer, firstViewport, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport* pViewports)
{
    ((PFN_vkCmdSetViewportWithCount)vk_device_table(commandBuffer)->Functions[VKD_vkCmdSetViewportWithCount])(commandBuffer, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
{
    ((PFN_vkCmdUpdateBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkCmdUpdateBuffer])(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    ((PFN_vkCmdWaitEvents)vk_device_table(commandBuffer)->Functions[VKD_vkCmdWaitEvents])(commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos)
{
    ((PFN_vkCmdWaitEvents2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdWaitEvents2])(commandBuffer, eventCount, pEvents, pDependencyInfos);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query)
{
    ((PFN_vkCmdWriteTimestamp)vk_device_table(commandBuffer)->Functions[VKD_vkCmdWriteTimestamp])(commandBuffer, pipelineStage, queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query)
{
    ((PFN_vkCmdWriteTimestamp2)vk_device_table(commandBuffer)->Functions[VKD_vkCmdWriteTimestamp2])(commandBuffer, stage, queryPool, query);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(VkDevice device, const VkCopyImageToImageInfo* pCopyImageToImageInfo)
{
    return ((PFN_vkCopyImageToImage)vk_device_table(device)->Functions[VKD_vkCopyImageToImage])(device, pCopyImageToImageInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemory(VkDevice device, const VkCopyImageToMemoryInfo* pCopyImageToMemoryInfo)
{
    return ((PFN_vkCopyImageToMemory)vk_device_table(device)->Functions[VKD_vkCopyImageToMemory])(device, pCopyImageToMemoryInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImage(VkDevice device, const VkCopyMemoryToImageInfo* pCopyMemoryToImageInfo)
{
    return ((PFN_vkCopyMemoryToImage)vk_device_table(device)->Functions[VKD_vkCopyMemoryToImage])(device, pCopyMemoryToImageInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    return ((PFN_vkCreateBuffer)vk_device_table(device)->Functions[VKD_vkCreateBuffer])(device, pCreateInfo, pAllocator, pBuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView)
{
    return ((PFN_vkCreateBufferView)vk_device_table(device)->Functions[VKD_vkCreateBufferView])(device, pCreateInfo, pAllocator, pView);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{
    return ((PFN_vkCreateCommandPool)vk_device_table(device)->Functions[VKD_vkCreateCommandPool])(device, pCreateInfo, pAllocator, pCommandPool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
    return ((PFN_vkCreateComputePipelines)vk_device_table(device)->Functions[VKD_vkCreateComputePipelines])(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
{
    return ((PFN_vkCreateDescriptorPool)vk_device_table(device)->Functions[VKD_vkCreateDescriptorPool])(device, pCreateInfo, pAllocator, pDescriptorPool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout)
{
    return ((PFN_vkCreateDescriptorSetLayout)vk_device_table(device)->Functions[VKD_vkCreateDescriptorSetLayout])(device, pCreateInfo, pAllocator, pSetLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate)
{
    return ((PFN_vkCreateDescriptorUpdateTemplate)vk_device_table(device)->Functions[VKD_vkCreateDescriptorUpdateTemplate])(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent)
{
    return ((PFN_vkCreateEvent)vk_device_table(device)->Functions[VKD_vkCreateEvent])(device, pCreateInfo, pAllocator, pEvent);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
{
    return ((PFN_vkCreateFence)vk_device_table(device)->Functions[VKD_vkCreateFence])(device, pCreateInfo, pAllocator, pFence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer)
{
    return ((PFN_vkCreateFramebuffer)vk_device_table(device)->Functions[VKD_vkCreateFramebuffer])(device, pCreateInfo, pAllocator, pFramebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
    return ((PFN_vkCreateGraphicsPipelines)vk_device_table(device)->Functions[VKD_vkCreateGraphicsPipelines])(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
    return ((PFN_vkCreateImage)vk_device_table(device)->Functions[VKD_vkCreateImage])(device, pCreateInfo, pAllocator, pImage);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{
    return ((PFN_vkCreateImageView)vk_device_table(device)->Functions[VKD_vkCreateImageView])(device, pCreateInfo, pAllocator, pView);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
{
    return ((PFN_vkCreatePipelineCache)vk_device_table(device)->Functions[VKD_vkCreatePipelineCache])(device, pCreateInfo, pAllocator, pPipelineCache);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
{
    return ((PFN_vkCreatePipelineLayout)vk_device_table(device)->Functions[VKD_vkCreatePipelineLayout])(device, pCreateInfo, pAllocator, pPipelineLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot)
{
    return ((PFN_vkCreatePrivateDataSlot)vk_device_table(device)->Functions[VKD_vkCreatePrivateDataSlot])(device, pCreateInfo, pAllocator, pPrivateDataSlot);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{
    return ((PFN_vkCreateQueryPool)vk_device_table(device)->Functions[VKD_vkCreateQueryPool])(device, pCreateInfo, pAllocator, pQueryPool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
    return ((PFN_vkCreateRenderPass)vk_device_table(device)->Functions[VKD_vkCreateRenderPass])(device, pCreateInfo, pAllocator, pRenderPass);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
    return ((PFN_vkCreateRenderPass2)vk_device_table(device)->Functions[VKD_vkCreateRenderPass2])(device, pCreateInfo, pAllocator, pRenderPass);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
{
    return ((PFN_vkCreateSampler)vk_device_table(device)->Functions[VKD_vkCreateSampler])(device, pCreateInfo, pAllocator, pSampler);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion)
{
    return ((PFN_vkCreateSamplerYcbcrConversion)vk_device_table(device)->Functions[VKD_vkCreateSamplerYcbcrConversion])(device, pCreateInfo, pAllocator, pYcbcrConversion);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
{
    return ((PFN_vkCreateSemaphore)vk_device_table(device)->Functions[VKD_vkCreateSemaphore])(device, pCreateInfo, pAllocator, pSemaphore);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule)
{
    return ((PFN_vkCreateShaderModule)vk_device_table(device)->Functions[VKD_vkCreateShaderModule])(device, pCreateInfo, pAllocator, pShaderModule);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains)
{
    return ((PFN_vkCreateSharedSwapchainsKHR)vk_device_table(device)->Functions[VKD_vkCreateSharedSwapchainsKHR])(device, swapchainCount, pCreateInfos, pAllocator, pSwapchains);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    return ((PFN_vkCreateSwapchainKHR)vk_device_table(device)->Functions[VKD_vkCreateSwapchainKHR])(device, pCreateInfo, pAllocator, pSwapchain);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyBuffer)vk_device_table(device)->Functions[VKD_vkDestroyBuffer])(device, buffer, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyBufferView)vk_device_table(device)->Functions[VKD_vkDestroyBufferView])(device, bufferView, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyCommandPool)vk_device_table(device)->Functions[VKD_vkDestroyCommandPool])(device, commandPool, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyDescriptorPool)vk_device_table(device)->Functions[VKD_vkDestroyDescriptorPool])(device, descriptorPool, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyDescriptorSetLayout)vk_device_table(device)->Functions[VKD_vkDestroyDescriptorSetLayout])(device, descriptorSetLayout, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyDescriptorUpdateTemplate)vk_device_table(device)->Functions[VKD_vkDestroyDescriptorUpdateTemplate])(device, descriptorUpdateTemplate, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyEvent)vk_device_table(device)->Functions[VKD_vkDestroyEvent])(device, event, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyFence)vk_device_table(device)->Functions[VKD_vkDestroyFence])(device, fence, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyFramebuffer)vk_device_table(device)->Functions[VKD_vkDestroyFramebuffer])(device, framebuffer, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyImage)vk_device_table(device)->Functions[VKD_vkDestroyImage])(device, image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyImageView)vk_device_table(device)->Functions[VKD_vkDestroyImageView])(device, imageView, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyPipeline)vk_device_table(device)->Functions[VKD_vkDestroyPipeline])(device, pipeline, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyPipelineCache)vk_device_table(device)->Functions[VKD_vkDestroyPipelineCache])(device, pipelineCache, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyPipelineLayout)vk_device_table(device)->Functions[VKD_vkDestroyPipelineLayout])(device, pipelineLayout, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyPrivateDataSlot)vk_device_table(device)->Functions[VKD_vkDestroyPrivateDataSlot])(device, privateDataSlot, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyQueryPool)vk_device_table(device)->Functions[VKD_vkDestroyQueryPool])(device, queryPool, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyRenderPass)vk_device_table(device)->Functions[VKD_vkDestroyRenderPass])(device, renderPass, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroySampler)vk_device_table(device)->Functions[VKD_vkDestroySampler])(device, sampler, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroySamplerYcbcrConversion)vk_device_table(device)->Functions[VKD_vkDestroySamplerYcbcrConversion])(device, ycbcrConversion, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroySemaphore)vk_device_table(device)->Functions[VKD_vkDestroySemaphore])(device, semaphore, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroyShaderModule)vk_device_table(device)->Functions[VKD_vkDestroyShaderModule])(device, shaderModule, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkDestroySwapchainKHR)vk_device_table(device)->Functions[VKD_vkDestroySwapchainKHR])(device, swapchain, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    return ((PFN_vkDeviceWaitIdle)vk_device_table(device)->Functions[VKD_vkDeviceWaitIdle])(device);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    return ((PFN_vkEndCommandBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkEndCommandBuffer])(commandBuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
{
    return ((PFN_vkFlushMappedMemoryRanges)vk_device_table(device)->Functions[VKD_vkFlushMappedMemoryRanges])(device, memoryRangeCount, pMemoryRanges);
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
{
    ((PFN_vkFreeCommandBuffers)vk_device_table(device)->Functions[VKD_vkFreeCommandBuffers])(device, commandPool, commandBufferCount, pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets)
{
    return ((PFN_vkFreeDescriptorSets)vk_device_table(device)->Functions[VKD_vkFreeDescriptorSets])(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
{
    ((PFN_vkFreeMemory)vk_device_table(device)->Functions[VKD_vkFreeMemory])(device, memory, pAllocator);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo)
{
    return ((PFN_vkGetBufferDeviceAddress)vk_device_table(device)->Functions[VKD_vkGetBufferDeviceAddress])(device, pInfo);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    ((PFN_vkGetBufferMemoryRequirements)vk_device_table(device)->Functions[VKD_vkGetBufferMemoryRequirements])(device, buffer, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
    ((PFN_vkGetBufferMemoryRequirements2)vk_device_table(device)->Functions[VKD_vkGetBufferMemoryRequirements2])(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo)
{
    return ((PFN_vkGetBufferOpaqueCaptureAddress)vk_device_table(device)->Functions[VKD_vkGetBufferOpaqueCaptureAddress])(device, pInfo);
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport)
{
    ((PFN_vkGetDescriptorSetLayoutSupport)vk_device_table(device)->Functions[VKD_vkGetDescriptorSetLayoutSupport])(device, pCreateInfo, pSupport);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
    ((PFN_vkGetDeviceBufferMemoryRequirements)vk_device_table(device)->Functions[VKD_vkGetDeviceBufferMemoryRequirements])(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures)
{
    ((PFN_vkGetDeviceGroupPeerMemoryFeatures)vk_device_table(device)->Functions[VKD_vkGetDeviceGroupPeerMemoryFeatures])(device, heapIndex, localDeviceIndex, remoteDeviceIndex, pPeerMemoryFeatures);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities)
{
    return ((PFN_vkGetDeviceGroupPresentCapabilitiesKHR)vk_device_table(device)->Functions[VKD_vkGetDeviceGroupPresentCapabilitiesKHR])(device, pDeviceGroupPresentCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes)
{
    return ((PFN_vkGetDeviceGroupSurfacePresentModesKHR)vk_device_table(device)->Functions[VKD_vkGetDeviceGroupSurfacePresentModesKHR])(device, surface, pModes);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
    ((PFN_vkGetDeviceImageMemoryRequirements)vk_device_table(device)->Functions[VKD_vkGetDeviceImageMemoryRequirements])(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
    ((PFN_vkGetDeviceImageSparseMemoryRequirements)vk_device_table(device)->Functions[VKD_vkGetDeviceImageSparseMemoryRequirements])(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayout(VkDevice device, const VkDeviceImageSubresourceInfo* pInfo, VkSubresourceLayout2* pLayout)
{
    ((PFN_vkGetDeviceImageSubresourceLayout)vk_device_table(device)->Functions[VKD_vkGetDeviceImageSubresourceLayout])(device, pInfo, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes)
{
    ((PFN_vkGetDeviceMemoryCommitment)vk_device_table(device)->Functions[VKD_vkGetDeviceMemoryCommitment])(device, memory, pCommittedMemoryInBytes);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
    return ((PFN_vkGetDeviceMemoryOpaqueCaptureAddress)vk_device_table(device)->Functions[VKD_vkGetDeviceMemoryOpaqueCaptureAddress])(device, pInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device, VkEvent event)
{
    return ((PFN_vkGetEventStatus)vk_device_table(device)->Functions[VKD_vkGetEventStatus])(device, event);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
    return ((PFN_vkGetFenceStatus)vk_device_table(device)->Functions[VKD_vkGetFenceStatus])(device, fence);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{
    ((PFN_vkGetImageMemoryRequirements)vk_device_table(device)->Functions[VKD_vkGetImageMemoryRequirements])(device, image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
    ((PFN_vkGetImageMemoryRequirements2)vk_device_table(device)->Functions[VKD_vkGetImageMemoryRequirements2])(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements)
{
    ((PFN_vkGetImageSparseMemoryRequirements)vk_device_table(device)->Functions[VKD_vkGetImageSparseMemoryRequirements])(device, image, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
    ((PFN_vkGetImageSparseMemoryRequirements2)vk_device_table(device)->Functions[VKD_vkGetImageSparseMemoryRequirements2])(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout)
{
    ((PFN_vkGetImageSubresourceLayout)vk_device_table(device)->Functions[VKD_vkGetImageSubresourceLayout])(device, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2(VkDevice device, VkImage image, const VkImageSubresource2* pSubresource, VkSubresourceLayout2* pLayout)
{
    ((PFN_vkGetImageSubresourceLayout2)vk_device_table(device)->Functions[VKD_vkGetImageSubresourceLayout2])(device, image, pSubresource, pLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData)
{
    return ((PFN_vkGetPipelineCacheData)vk_device_table(device)->Functions[VKD_vkGetPipelineCacheData])(device, pipelineCache, pDataSize, pData);
}

VKAPI_ATTR void VKAPI_CALL vkGetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t* pData)
{
    ((PFN_vkGetPrivateData)vk_device_table(device)->Functions[VKD_vkGetPrivateData])(device, objectType, objectHandle, privateDataSlot, pData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags)
{
    return ((PFN_vkGetQueryPoolResults)vk_device_table(device)->Functions[VKD_vkGetQueryPoolResults])(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity)
{
    ((PFN_vkGetRenderAreaGranularity)vk_device_table(device)->Functions[VKD_vkGetRenderAreaGranularity])(device, renderPass, pGranularity);
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularity(VkDevice device, const VkRenderingAreaInfo* pRenderingAreaInfo, VkExtent2D* pGranularity)
{
    ((PFN_vkGetRenderingAreaGranularity)vk_device_table(device)->Functions[VKD_vkGetRenderingAreaGranularity])(device, pRenderingAreaInfo, pGranularity);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t* pValue)
{
    return ((PFN_vkGetSemaphoreCounterValue)vk_device_table(device)->Functions[VKD_vkGetSemaphoreCounterValue])(device, semaphore, pValue);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
{
    return ((PFN_vkGetSwapchainImagesKHR)vk_device_table(device)->Functions[VKD_vkGetSwapchainImagesKHR])(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
{
    return ((PFN_vkInvalidateMappedMemoryRanges)vk_device_table(device)->Functions[VKD_vkInvalidateMappedMemoryRanges])(device, memoryRangeCount, pMemoryRanges);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
{
    return ((PFN_vkMapMemory)vk_device_table(device)->Functions[VKD_vkMapMemory])(device, memory, offset, size, flags, ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData)
{
    return ((PFN_vkMapMemory2)vk_device_table(device)->Functions[VKD_vkMapMemory2])(device, pMemoryMapInfo, ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches)
{
    return ((PFN_vkMergePipelineCaches)vk_device_table(device)->Functions[VKD_vkMergePipelineCaches])(device, dstCache, srcCacheCount, pSrcCaches);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence)
{
    return ((PFN_vkQueueBindSparse)vk_device_table(queue)->Functions[VKD_vkQueueBindSparse])(queue, bindInfoCount, pBindInfo, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    return ((PFN_vkQueuePresentKHR)vk_device_table(queue)->Functions[VKD_vkQueuePresentKHR])(queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    return ((PFN_vkQueueSubmit)vk_device_table(queue)->Functions[VKD_vkQueueSubmit])(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence)
{
    return ((PFN_vkQueueSubmit2)vk_device_table(queue)->Functions[VKD_vkQueueSubmit2])(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    return ((PFN_vkQueueWaitIdle)vk_device_table(queue)->Functions[VKD_vkQueueWaitIdle])(queue);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    return ((PFN_vkResetCommandBuffer)vk_device_table(commandBuffer)->Functions[VKD_vkResetCommandBuffer])(commandBuffer, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
{
    return ((PFN_vkResetCommandPool)vk_device_table(device)->Functions[VKD_vkResetCommandPool])(device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
    return ((PFN_vkResetDescriptorPool)vk_device_table(device)->Functions[VKD_vkResetDescriptorPool])(device, descriptorPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event)
{
    return ((PFN_vkResetEvent)vk_device_table(device)->Functions[VKD_vkResetEvent])(device, event);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
{
    return ((PFN_vkResetFences)vk_device_table(device)->Functions[VKD_vkResetFences])(device, fenceCount, pFences);
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    ((PFN_vkResetQueryPool)vk_device_table(device)->Functions[VKD_vkResetQueryPool])(device, queryPool, firstQuery, queryCount);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event)
{
    return ((PFN_vkSetEvent)vk_device_table(device)->Functions[VKD_vkSetEvent])(device, event);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data)
{
    return ((PFN_vkSetPrivateData)vk_device_table(device)->Functions[VKD_vkSetPrivateData])(device, objectType, objectHandle, privateDataSlot, data);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo)
{
    return ((PFN_vkSignalSemaphore)vk_device_table(device)->Functions[VKD_vkSignalSemaphore])(device, pSignalInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayout(VkDevice device, uint32_t transitionCount, const VkHostImageLayoutTransitionInfo* pTransitions)
{
    return ((PFN_vkTransitionImageLayout)vk_device_table(device)->Functions[VKD_vkTransitionImageLayout])(device, transitionCount, pTransitions);
}

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags)
{
    ((PFN_vkTrimCommandPool)vk_device_table(device)->Functions[VKD_vkTrimCommandPool])(device, commandPool, flags);
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    ((PFN_vkUnmapMemory)vk_device_table(device)->Functions[VKD_vkUnmapMemory])(device, memory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo)
{
    return ((PFN_vkUnmapMemory2)vk_device_table(device)->Functions[VKD_vkUnmapMemory2])(device, pMemoryUnmapInfo);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData)
{
    ((PFN_vkUpdateDescriptorSetWithTemplate)vk_device_table(device)->Functions[VKD_vkUpdateDescriptorSetWithTemplate])(device, descriptorSet, descriptorUpdateTemplate, pData);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{
    ((PFN_vkUpdateDescriptorSets)vk_device_table(device)->Functions[VKD_vkUpdateDescriptorSets])(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
{
    return ((PFN_vkWaitForFences)vk_device_table(device)->Functions[VKD_vkWaitForFences])(device, fenceCount, pFences, waitAll, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout)
{
    return ((PFN_vkWaitSemaphores)vk_device_table(device)->Functions[VKD_vkWaitSemaphores])(device, pWaitInfo, timeout);
}

const struct vk_export vk_exports[] =
{
    { "vkAcquireNextImage2KHR", (PFN_vkVoidFunction)vkAcquireNextImage2KHR },
    { "vkAcquireNextImageKHR", (PFN_vkVoidFunction)vkAcquireNextImageKHR },
    { "vkAllocateCommandBuffers", (PFN_vkVoidFunction)vkAllocateCommandBuffers },
    { "vkAllocateDescriptorSets", (PFN_vkVoidFunction)vkAllocateDescriptorSets },
    { "vkAllocateMemory", (PFN_vkVoidFunction)vkAllocateMemory },
    { "vkBeginCommandBuffer", (PFN_vkVoidFunction)vkBeginCommandBuffer },
    { "vkBindBufferMemory", (PFN_vkVoidFunction)vkBindBufferMemory },
    { "vkBindBufferMemory2", (PFN_vkVoidFunction)vkBindBufferMemory2 },
    { "vkBindImageMemory", (PFN_vkVoidFunction)vkBindImageMemory },
    { "vkBindImageMemory2", (PFN_vkVoidFunction)vkBindImageMemory2 },
    { "vkCmdBeginQuery", (PFN_vkVoidFunction)vkCmdBeginQuery },
    { "vkCmdBeginRenderPass", (PFN_vkVoidFunction)vkCmdBeginRenderPass },
    { "vkCmdBeginRenderPass2", (PFN_vkVoidFunction)vkCmdBeginRenderPass2 },
    { "vkCmdBeginRendering", (PFN_vkVoidFunction)vkCmdBeginRendering },
    { "vkCmdBindDescriptorSets", (PFN_vkVoidFunction)vkCmdBindDescriptorSets },
    { "vkCmdBindDescriptorSets2", (PFN_vkVoidFunction)vkCmdBindDescriptorSets2 },
    { "vkCmdBindIndexBuffer", (PFN_vkVoidFunction)vkCmdBindIndexBuffer },
    { "vkCmdBindIndexBuffer2", (PFN_vkVoidFunction)vkCmdBindIndexBuffer2 },
    { "vkCmdBindPipeline", (PFN_vkVoidFunction)vkCmdBindPipeline },
    { "vkCmdBindVertexBuffers", (PFN_vkVoidFunction)vkCmdBindVertexBuffers },
    { "vkCmdBindVertexBuffers2", (PFN_vkVoidFunction)vkCmdBindVertexBuffers2 },
    { "vkCmdBlitImage", (PFN_vkVoidFunction)vkCmdBlitImage },
    { "vkCmdBlitImage2", (PFN_vkVoidFunction)vkCmdBlitImage2 },
    { "vkCmdClearAttachments", (PFN_vkVoidFunction)vkCmdClearAttachments },
    { "vkCmdClearColorImage", (PFN_vkVoidFunction)vkCmdClearColorImage },
    { "vkCmdClearDepthStencilImage", (PFN_vkVoidFunction)vkCmdClearDepthStencilImage },
    { "vkCmdCopyBuffer", (PFN_vkVoidFunction)vkCmdCopyBuffer },
    { "vkCmdCopyBuffer2", (PFN_vkVoidFunction)vkCmdCopyBuffer2 },
    { "vkCmdCopyBufferToImage", (PFN_vkVoidFunction)vkCmdCopyBufferToImage },
    { "vkCmdCopyBufferToImage2", (PFN_vkVoidFunction)vkCmdCopyBufferToImage2 },
    { "vkCmdCopyImage", (PFN_vkVoidFunction)vkCmdCopyImage },
    { "vkCmdCopyImage2", (PFN_vkVoidFunction)vkCmdCopyImage2 },
    { "vkCmdCopyImageToBuffer", (PFN_vkVoidFunction)vkCmdCopyImageToBuffer },
    { "vkCmdCopyImageToBuffer2", (PFN_vkVoidFunction)vkCmdCopyImageToBuffer2 },
    { "vkCmdCopyQueryPoolResults", (PFN_vkVoidFunction)vkCmdCopyQueryPoolResults },
    { "vkCmdDispatch", (PFN_vkVoidFunction)vkCmdDispatch },
    { "vkCmdDispatchBase", (PFN_vkVoidFunction)vkCmdDispatchBase },
    { "vkCmdDispatchIndirect", (PFN_vkVoidFunction)vkCmdDispatchIndirect },
    { "vkCmdDraw", (PFN_vkVoidFunction)vkCmdDraw },
    { "vkCmdDrawIndexed", (PFN_vkVoidFunction)vkCmdDrawIndexed },
    { "vkCmdDrawIndexedIndirect", (PFN_vkVoidFunction)vkCmdDrawIndexedIndirect },
    { "vkCmdDrawIndexedIndirectCount", (PFN_vkVoidFunction)vkCmdDrawIndexedIndirectCount },
    { "vkCmdDrawIndirect", (PFN_vkVoidFunction)vkCmdDrawIndirect },
    { "vkCmdDrawIndirectCount", (PFN_vkVoidFunction)vkCmdDrawIndirectCount },
    { "vkCmdEndQuery", (PFN_vkVoidFunction)vkCmdEndQuery },
    { "vkCmdEndRenderPass", (PFN_vkVoidFunction)vkCmdEndRenderPass },
    { "vkCmdEndRenderPass2", (PFN_vkVoidFunction)vkCmdEndRenderPass2 },
    { "vkCmdEndRendering", (PFN_vkVoidFunction)vkCmdEndRendering },
    { "vkCmdExecuteCommands", (PFN_vkVoidFunction)vkCmdExecuteCommands },
    { "vkCmdFillBuffer", (PFN_vkVoidFunction)vkCmdFillBuffer },
    { "vkCmdNextSubpass", (PFN_vkVoidFunction)vkCmdNextSubpass },
    { "vkCmdNextSubpass2", (PFN_vkVoidFunction)vkCmdNextSubpass2 },
    { "vkCmdPipelineBarrier", (PFN_vkVoidFunction)vkCmdPipelineBarrier },
    { "vkCmdPipelineBarrier2", (PFN_vkVoidFunction)vkCmdPipelineBarrier2 },
    { "vkCmdPushConstants", (PFN_vkVoidFunction)vkCmdPushConstants },
    { "vkCmdPushConstants2", (PFN_vkVoidFunction)vkCmdPushConstants2 },
    { "vkCmdPushDescriptorSet", (PFN_vkVoidFunction)vkCmdPushDescriptorSet },
    { "vkCmdPushDescriptorSet2", (PFN_vkVoidFunction)vkCmdPushDescriptorSet2 },
    { "vkCmdPushDescriptorSetWithTemplate", (PFN_vkVoidFunction)vkCmdPushDescriptorSetWithTemplate },
    { "vkCmdPushDescriptorSetWithTemplate2", (PFN_vkVoidFunction)vkCmdPushDescriptorSetWithTemplate2 },
    { "vkCmdResetEvent", (PFN_vkVoidFunction)vkCmdResetEvent },
    { "vkCmdResetEvent2", (PFN_vkVoidFunction)vkCmdResetEvent2 },
    { "vkCmdResetQueryPool", (PFN_vkVoidFunction)vkCmdResetQueryPool },
    { "vkCmdResolveImage", (PFN_vkVoidFunction)vkCmdResolveImage },
    { "vkCmdResolveImage2", (PFN_vkVoidFunction)vkCmdResolveImage2 },
    { "vkCmdSetBlendConstants", (PFN_vkVoidFunction)vkCmdSetBlendConstants },
    { "vkCmdSetCullMode", (PFN_vkVoidFunction)vkCmdSetCullMode },
    { "vkCmdSetDepthBias", (PFN_vkVoidFunction)vkCmdSetDepthBias },
    { "vkCmdSetDepthBiasEnable", (PFN_vkVoidFunction)vkCmdSetDepthBiasEnable },
    { "vkCmdSetDepthBounds", (PFN_vkVoidFunction)vkCmdSetDepthBounds },
    { "vkCmdSetDepthBoundsTestEnable", (PFN_vkVoidFunction)vkCmdSetDepthBoundsTestEnable },
    { "vkCmdSetDepthCompareOp", (PFN_vkVoidFunction)vkCmdSetDepthCompareOp },
    { "vkCmdSetDepthTestEnable", (PFN_vkVoidFunction)vkCmdSetDepthTestEnable },
    { "vkCmdSetDepthWriteEnable", (PFN_vkVoidFunction)vkCmdSetDepthWriteEnable },
    { "vkCmdSetDeviceMask", (PFN_vkVoidFunction)vkCmdSetDeviceMask },
    { "vkCmdSetEvent", (PFN_vkVoidFunction)vkCmdSetEvent },
    { "vkCmdSetEvent2", (PFN_vkVoidFunction)vkCmdSetEvent2 },
    { "vkCmdSetFrontFace", (PFN_vkVoidFunction)vkCmdSetFrontFace },
    { "vkCmdSetLineStipple", (PFN_vkVoidFunction)vkCmdSetLineStipple },
    { "vkCmdSetLineWidth", (PFN_vkVoidFunction)vkCmdSetLineWidth },
    { "vkCmdSetPrimitiveRestartEnable", (PFN_vkVoidFunction)vkCmdSetPrimitiveRestartEnable },
    { "vkCmdSetPrimitiveTopology", (PFN_vkVoidFunction)vkCmdSetPrimitiveTopology },
    { "vkCmdSetRasterizerDiscardEnable", (PFN_vkVoidFunction)vkCmdSetRasterizerDiscardEnable },
    { "vkCmdSetRenderingAttachmentLocations", (PFN_vkVoidFunction)vkCmdSetRenderingAttachmentLocations },
    { "vkCmdSetRenderingInputAttachmentIndices", (PFN_vkVoidFunction)vkCmdSetRenderingInputAttachmentIndices },
    { "vkCmdSetScissor", (PFN_vkVoidFunction)vkCmdSetScissor },
    { "vkCmdSetScissorWithCount", (PFN_vkVoidFunction)vkCmdSetScissorWithCount },
    { "vkCmdSetStencilCompareMask", (PFN_vkVoidFunction)vkCmdSetStencilCompareMask },
    { "vkCmdSetStencilOp", (PFN_vkVoidFunction)vkCmdSetStencilOp },
    { "vkCmdSetStencilReference", (PFN_vkVoidFunction)vkCmdSetStencilReference },
    { "vkCmdSetStencilTestEnable", (PFN_vkVoidFunction)vkCmdSetStencilTestEnable },
    { "vkCmdSetStencilWriteMask", (PFN_vkVoidFunction)vkCmdSetStencilWriteMask },
    { "vkCmdSetViewport", (PFN_vkVoidFunction)vkCmdSetViewport },
    { "vkCmdSetViewportWithCount", (PFN_vkVoidFunction)vkCmdSetViewportWithCount },
    { "vkCmdUpdateBuffer", (PFN_vkVoidFunction)vkCmdUpdateBuffer },
    { "vkCmdWaitEvents", (PFN_vkVoidFunction)vkCmdWaitEvents },
    { "vkCmdWaitEvents2", (PFN_vkVoidFunction)vkCmdWaitEvents2 },
    { "vkCmdWriteTimestamp", (PFN_vkVoidFunction)vkCmdWriteTimestamp },
    { "vkCmdWriteTimestamp2", (PFN_vkVoidFunction)vkCmdWriteTimestamp2 },
    { "vkCopyImageToImage", (PFN_vkVoidFunction)vkCopyImageToImage },
    { "vkCopyImageToMemory", (PFN_vkVoidFunction)vkCopyImageToMemory },
    { "vkCopyMemoryToImage", (PFN_vkVoidFunction)vkCopyMemoryToImage },
    { "vkCreateBuffer", (PFN_vkVoidFunction)vkCreateBuffer },
    { "vkCreateBufferView", (PFN_vkVoidFunction)vkCreateBufferView },
    { "vkCreateCommandPool", (PFN_vkVoidFunction)vkCreateCommandPool },
    { "vkCreateComputePipelines", (PFN_vkVoidFunction)vkCreateComputePipelines },
    { "vkCreateDescriptorPool", (PFN_vkVoidFunction)vkCreateDescriptorPool },
    { "vkCreateDescriptorSetLayout", (PFN_vkVoidFunction)vkCreateDescriptorSetLayout },
    { "vkCreateDescriptorUpdateTemplate", (PFN_vkVoidFunction)vkCreateDescriptorUpdateTemplate },
    { "vkCreateDevice", (PFN_vkVoidFunction)vkCreateDevice },
    { "vkCreateDisplayModeKHR", (PFN_vkVoidFunction)vkCreateDisplayModeKHR },
    { "vkCreateDisplayPlaneSurfaceKHR", (PFN_vkVoidFunction)vkCreateDisplayPlaneSurfaceKHR },
    { "vkCreateEvent", (PFN_vkVoidFunction)vkCreateEvent },
    { "vkCreateFence", (PFN_vkVoidFunction)vkCreateFence },
    { "vkCreateFramebuffer", (PFN_vkVoidFunction)vkCreateFramebuffer },
    { "vkCreateGraphicsPipelines", (PFN_vkVoidFunction)vkCreateGraphicsPipelines },
    { "vkCreateImage", (PFN_vkVoidFunction)vkCreateImage },
    { "vkCreateImageView", (PFN_vkVoidFunction)vkCreateImageView },
    { "vkCreateInstance", (PFN_vkVoidFunction)vkCreateInstance },
    { "vkCreatePipelineCache", (PFN_vkVoidFunction)vkCreatePipelineCache },
    { "vkCreatePipelineLayout", (PFN_vkVoidFunction)vkCreatePipelineLayout },
    { "vkCreatePrivateDataSlot", (PFN_vkVoidFunction)vkCreatePrivateDataSlot },
    { "vkCreateQueryPool", (PFN_vkVoidFunction)vkCreateQueryPool },
    { "vkCreateRenderPass", (PFN_vkVoidFunction)vkCreateRenderPass },
    { "vkCreateRenderPass2", (PFN_vkVoidFunction)vkCreateRenderPass2 },
    { "vkCreateSampler", (PFN_vkVoidFunction)vkCreateSampler },
    { "vkCreateSamplerYcbcrConversion", (PFN_vkVoidFunction)vkCreateSamplerYcbcrConversion },
    { "vkCreateSemaphore", (PFN_vkVoidFunction)vkCreateSemaphore },
    { "vkCreateShaderModule", (PFN_vkVoidFunction)vkCreateShaderModule },
    { "vkCreateSharedSwapchainsKHR", (PFN_vkVoidFunction)vkCreateSharedSwapchainsKHR },
    { "vkCreateSwapchainKHR", (PFN_vkVoidFunction)vkCreateSwapchainKHR },
    { "vkCreateWin32SurfaceKHR", (PFN_vkVoidFunction)vkCreateWin32SurfaceKHR },
    { "vkDestroyBuffer", (PFN_vkVoidFunction)vkDestroyBuffer },
    { "vkDestroyBufferView", (PFN_vkVoidFunction)vkDestroyBufferView },
    { "vkDestroyCommandPool", (PFN_vkVoidFunction)vkDestroyCommandPool },
    { "vkDestroyDescriptorPool", (PFN_vkVoidFunction)vkDestroyDescriptorPool },
    { "vkDestroyDescriptorSetLayout", (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout },
    { "vkDestroyDescriptorUpdateTemplate", (PFN_vkVoidFunction)vkDestroyDescriptorUpdateTemplate },
    { "vkDestroyDevice", (PFN_vkVoidFunction)vkDestroyDevice },
    { "vkDestroyEvent", (PFN_vkVoidFunction)vkDestroyEvent },
    { "vkDestroyFence", (PFN_vkVoidFunction)vkDestroyFence },
    { "vkDestroyFramebuffer", (PFN_vkVoidFunction)vkDestroyFramebuffer },
    { "vkDestroyImage", (PFN_vkVoidFunction)vkDestroyImage },
    { "vkDestroyImageView", (PFN_vkVoidFunction)vkDestroyImageView },
    { "vkDestroyInstance", (PFN_vkVoidFunction)vkDestroyInstance },
    { "vkDestroyPipeline", (PFN_vkVoidFunction)vkDestroyPipeline },
    { "vkDestroyPipelineCache", (PFN_vkVoidFunction)vkDestroyPipelineCache },
    { "vkDestroyPipelineLayout", (PFN_vkVoidFunction)vkDestroyPipelineLayout },
    { "vkDestroyPrivateDataSlot", (PFN_vkVoidFunction)vkDestroyPrivateDataSlot },
    { "vkDestroyQueryPool", (PFN_vkVoidFunction)vkDestroyQueryPool },
    { "vkDestroyRenderPass", (PFN_vkVoidFunction)vkDestroyRenderPass },
    { "vkDestroySampler", (PFN_vkVoidFunction)vkDestroySampler },
    { "vkDestroySamplerYcbcrConversion", (PFN_vkVoidFunction)vkDestroySamplerYcbcrConversion },
    { "vkDestroySemaphore", (PFN_vkVoidFunction)vkDestroySemaphore },
    { "vkDestroyShaderModule", (PFN_vkVoidFunction)vkDestroyShaderModule },
    { "vkDestroySurfaceKHR", (PFN_vkVoidFunction)vkDestroySurfaceKHR },
    { "vkDestroySwapchainKHR", (PFN_vkVoidFunction)vkDestroySwapchainKHR },
    { "vkDeviceWaitIdle", (PFN_vkVoidFunction)vkDeviceWaitIdle },
    { "vkEndCommandBuffer", (PFN_vkVoidFunction)vkEndCommandBuffer },
    { "vkEnumerateDeviceExtensionProperties", (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties },
    { "vkEnumerateDeviceLayerProperties", (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties },
    { "vkEnumerateInstanceExtensionProperties", (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties },
    { "vkEnumerateInstanceLayerProperties", (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties },
    { "vkEnumerateInstanceVersion", (PFN_vkVoidFunction)vkEnumerateInstanceVersion },
    { "vkEnumeratePhysicalDeviceGroups", (PFN_vkVoidFunction)vkEnumeratePhysicalDeviceGroups },
    { "vkEnumeratePhysicalDevices", (PFN_vkVoidFunction)vkEnumeratePhysicalDevices },
    { "vkFlushMappedMemoryRanges", (PFN_vkVoidFunction)vkFlushMappedMemoryRanges },
    { "vkFreeCommandBuffers", (PFN_vkVoidFunction)vkFreeCommandBuffers },
    { "vkFreeDescriptorSets", (PFN_vkVoidFunction)vkFreeDescriptorSets },
    { "vkFreeMemory", (PFN_vkVoidFunction)vkFreeMemory },
    { "vkGetBufferDeviceAddress", (PFN_vkVoidFunction)vkGetBufferDeviceAddress },
    { "vkGetBufferMemoryRequirements", (PFN_vkVoidFunction)vkGetBufferMemoryRequirements },
    { "vkGetBufferMemoryRequirements2", (PFN_vkVoidFunction)vkGetBufferMemoryRequirements2 },
    { "vkGetBufferOpaqueCaptureAddress", (PFN_vkVoidFunction)vkGetBufferOpaqueCaptureAddress },
    { "vkGetDescriptorSetLayoutSupport", (PFN_vkVoidFunction)vkGetDescriptorSetLayoutSupport },
    { "vkGetDeviceBufferMemoryRequirements", (PFN_vkVoidFunction)vkGetDeviceBufferMemoryRequirements },
    { "vkGetDeviceGroupPeerMemoryFeatures", (PFN_vkVoidFunction)vkGetDeviceGroupPeerMemoryFeatures },
    { "vkGetDeviceGroupPresentCapabilitiesKHR", (PFN_vkVoidFunction)vkGetDeviceGroupPresentCapabilitiesKHR },
    { "vkGetDeviceGroupSurfacePresentModesKHR", (PFN_vkVoidFunction)vkGetDeviceGroupSurfacePresentModesKHR },
    { "vkGetDeviceImageMemoryRequirements", (PFN_vkVoidFunction)vkGetDeviceImageMemoryRequirements },
    { "vkGetDeviceImageSparseMemoryRequirements", (PFN_vkVoidFunction)vkGetDeviceImageSparseMemoryRequirements },
    { "vkGetDeviceImageSubresourceLayout", (PFN_vkVoidFunction)vkGetDeviceImageSubresourceLayout },
    { "vkGetDeviceMemoryCommitment", (PFN_vkVoidFunction)vkGetDeviceMemoryCommitment },
    { "vkGetDeviceMemoryOpaqueCaptureAddress", (PFN_vkVoidFunction)vkGetDeviceMemoryOpaqueCaptureAddress },
    { "vkGetDeviceProcAddr", (PFN_vkVoidFunction)vkGetDeviceProcAddr },
    { "vkGetDeviceQueue", (PFN_vkVoidFunction)vkGetDeviceQueue },
    { "vkGetDeviceQueue2", (PFN_vkVoidFunction)vkGetDeviceQueue2 },
    { "vkGetDisplayModePropertiesKHR", (PFN_vkVoidFunction)vkGetDisplayModePropertiesKHR },
    { "vkGetDisplayPlaneCapabilitiesKHR", (PFN_vkVoidFunction)vkGetDisplayPlaneCapabilitiesKHR },
    { "vkGetDisplayPlaneSupportedDisplaysKHR", (PFN_vkVoidFunction)vkGetDisplayPlaneSupportedDisplaysKHR },
    { "vkGetEventStatus", (PFN_vkVoidFunction)vkGetEventStatus },
    { "vkGetFenceStatus", (PFN_vkVoidFunction)vkGetFenceStatus },
    { "vkGetImageMemoryRequirements", (PFN_vkVoidFunction)vkGetImageMemoryRequirements },
    { "vkGetImageMemoryRequirements2", (PFN_vkVoidFunction)vkGetImageMemoryRequirements2 },
    { "vkGetImageSparseMemoryRequirements", (PFN_vkVoidFunction)vkGetImageSparseMemoryRequirements },
    { "vkGetImageSparseMemoryRequirements2", (PFN_vkVoidFunction)vkGetImageSparseMemoryRequirements2 },
    { "vkGetImageSubresourceLayout", (PFN_vkVoidFunction)vkGetImageSubresourceLayout },
    { "vkGetImageSubresourceLayout2", (PFN_vkVoidFunction)vkGetImageSubresourceLayout2 },
    { "vkGetInstanceProcAddr", (PFN_vkVoidFunction)vkGetInstanceProcAddr },
    { "vkGetPhysicalDeviceDisplayPlanePropertiesKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceDisplayPlanePropertiesKHR },
    { "vkGetPhysicalDeviceDisplayPropertiesKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceDisplayPropertiesKHR },
    { "vkGetPhysicalDeviceExternalBufferProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceExternalBufferProperties },
    { "vkGetPhysicalDeviceExternalFenceProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceExternalFenceProperties },
    { "vkGetPhysicalDeviceExternalSemaphoreProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceExternalSemaphoreProperties },
    { "vkGetPhysicalDeviceFeatures", (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures },
    { "vkGetPhysicalDeviceFeatures2", (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2 },
    { "vkGetPhysicalDeviceFormatProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties },
    { "vkGetPhysicalDeviceFormatProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties2 },
    { "vkGetPhysicalDeviceImageFormatProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties },
    { "vkGetPhysicalDeviceImageFormatProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties2 },
    { "vkGetPhysicalDeviceMemoryProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties },
    { "vkGetPhysicalDeviceMemoryProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties2 },
    { "vkGetPhysicalDevicePresentRectanglesKHR", (PFN_vkVoidFunction)vkGetPhysicalDevicePresentRectanglesKHR },
    { "vkGetPhysicalDeviceProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties },
    { "vkGetPhysicalDeviceProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2 },
    { "vkGetPhysicalDeviceQueueFamilyProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties },
    { "vkGetPhysicalDeviceQueueFamilyProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties2 },
    { "vkGetPhysicalDeviceSparseImageFormatProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties },
    { "vkGetPhysicalDeviceSparseImageFormatProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties2 },
    { "vkGetPhysicalDeviceSurfaceCapabilities2KHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilities2KHR },
    { "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR },
    { "vkGetPhysicalDeviceSurfaceFormats2KHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormats2KHR },
    { "vkGetPhysicalDeviceSurfaceFormatsKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR },
    { "vkGetPhysicalDeviceSurfacePresentModesKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR },
    { "vkGetPhysicalDeviceSurfaceSupportKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceSupportKHR },
    { "vkGetPhysicalDeviceToolProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceToolProperties },
    { "vkGetPhysicalDeviceWin32PresentationSupportKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceWin32PresentationSupportKHR },
    { "vkGetPipelineCacheData", (PFN_vkVoidFunction)vkGetPipelineCacheData },
    { "vkGetPrivateData", (PFN_vkVoidFunction)vkGetPrivateData },
    { "vkGetQueryPoolResults", (PFN_vkVoidFunction)vkGetQueryPoolResults },
    { "vkGetRenderAreaGranularity", (PFN_vkVoidFunction)vkGetRenderAreaGranularity },
    { "vkGetRenderingAreaGranularity", (PFN_vkVoidFunction)vkGetRenderingAreaGranularity },
    { "vkGetSemaphoreCounterValue", (PFN_vkVoidFunction)vkGetSemaphoreCounterValue },
    { "vkGetSwapchainImagesKHR", (PFN_vkVoidFunction)vkGetSwapchainImagesKHR },
    { "vkInvalidateMappedMemoryRanges", (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges },
    { "vkMapMemory", (PFN_vkVoidFunction)vkMapMemory },
    { "vkMapMemory2", (PFN_vkVoidFunction)vkMapMemory2 },
    { "vkMergePipelineCaches", (PFN_vkVoidFunction)vkMergePipelineCaches },
    { "vkQueueBindSparse", (PFN_vkVoidFunction)vkQueueBindSparse },
    { "vkQueuePresentKHR", (PFN_vkVoidFunction)vkQueuePresentKHR },
    { "vkQueueSubmit", (PFN_vkVoidFunction)vkQueueSubmit },
    { "vkQueueSubmit2", (PFN_vkVoidFunction)vkQueueSubmit2 },
    { "vkQueueWaitIdle", (PFN_vkVoidFunction)vkQueueWaitIdle },
    { "vkResetCommandBuffer", (PFN_vkVoidFunction)vkResetCommandBuffer },
    { "vkResetCommandPool", (PFN_vkVoidFunction)vkResetCommandPool },
    { "vkResetDescriptorPool", (PFN_vkVoidFunction)vkResetDescriptorPool },
    { "vkResetEvent", (PFN_vkVoidFunction)vkResetEvent },
    { "vkResetFences", (PFN_vkVoidFunction)vkResetFences },
    { "vkResetQueryPool", (PFN_vkVoidFunction)vkResetQueryPool },
    { "vkSetEvent", (PFN_vkVoidFunction)vkSetEvent },
    { "vkSetPrivateData", (PFN_vkVoidFunction)vkSetPrivateData },
    { "vkSignalSemaphore", (PFN_vkVoidFunction)vkSignalSemaphore },
    { "vkTransitionImageLayout", (PFN_vkVoidFunction)vkTransitionImageLayout },
    { "vkTrimCommandPool", (PFN_vkVoidFunction)vkTrimCommandPool },
    { "vkUnmapMemory", (PFN_vkVoidFunction)vkUnmapMemory },
    { "vkUnmapMemory2", (PFN_vkVoidFunction)vkUnmapMemory2 },
    { "vkUpdateDescriptorSetWithTemplate", (PFN_vkVoidFunction)vkUpdateDescriptorSetWithTemplate },
    { "vkUpdateDescriptorSets", (PFN_vkVoidFunction)vkUpdateDescriptorSets },
    { "vkWaitForFences", (PFN_vkVoidFunction)vkWaitForFences },
    { "vkWaitSemaphores", (PFN_vkVoidFunction)vkWaitSemaphores },
};

const unsigned int vk_export_count = sizeof(vk_exports) / sizeof(vk_exports[0]);
