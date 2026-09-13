/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_video_dec.h"
#include "ac_vcn_dec.h"
#include "ac_uvd_dec.h"

uint32_t
ac_video_dec_dpb_size(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   if (info->vcn_ip_version >= VCN_1_0_0)
      return ac_vcn_dec_dpb_size(info, param);
   else if (info->ip[AMD_IP_UVD].num_queues)
      return ac_uvd_dec_dpb_size(info, param);
   return 0;
}

uint32_t
ac_video_dec_dpb_alignment(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   if (info->vcn_ip_version >= VCN_1_0_0)
      return ac_vcn_dec_dpb_alignment(info, param);
   return 0;
}

struct ac_video_dec *
ac_create_video_decoder(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   if (info->vcn_ip_version >= VCN_1_0_0) {
      if (param->codec == AC_VIDEO_CODEC_MJPEG)
         return ac_vcn_create_jpeg_decoder(info, param);
      else
         return ac_vcn_create_video_decoder(info, param);
   } else if (info->ip[AMD_IP_UVD].num_queues) {
      return ac_uvd_create_video_decoder(info, param);
   }
   return NULL;
}
