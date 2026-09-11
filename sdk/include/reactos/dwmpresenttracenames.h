/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#ifndef ROS_DWM_PRESENT_TRACE_NAMES_H
#define ROS_DWM_PRESENT_TRACE_NAMES_H
#include "dwmpresenttrace.h"

static __inline const char *DptMetricName(ULONG Metric)
{
    static const char *Names[] = {
        "dirty_frame_fetch_to_ack", "metadata_fetch", "scene_prepare", "gpu_begin",
        "window", "texture_layer", "blur", "shadow", "swap", "surface_ack",
        "mesa_present", "primary_query", "scanout_blit_issue", "flush",
        "device_lock_wait", "primary_fence_wait", "primary_cache_invalidate",
        "kmt_present", "kmt_submit", "winsys_ioctl", "sampler_shadow_update",
        "kernel_command_admit", "kernel_track", "miniport_render", "miniport_submit",
        "kernel_present", "admit_to_dispatch", "dispatch_to_retire", "fence_publish",
        "blur_cache_hit", "blur_filter", "bo_create", "cpu_texture_upload",
        "wgl_flush", "wgl_swap_delay", "wgl_present_callback", "shared_surface_compose",
        "buffer_fence_wait"
    };
    C_ASSERT(sizeof(Names) / sizeof(Names[0]) == DPT_METRIC_COUNT);
    return Metric < DPT_METRIC_COUNT ? Names[Metric] : "unknown";
}
#endif
