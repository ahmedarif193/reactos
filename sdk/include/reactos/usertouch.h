/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private USER calls for per-window touch registration
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */
#pragma once

#define ROS_TOUCH_REGISTER       0x1007
#define ROS_TOUCH_UNREGISTER     0x1008
#define ROS_TOUCH_QUERY          0x1009
#define ROS_TOUCH_REGISTERED     0x80000000UL
#define ROS_TOUCH_VALID_FLAGS    0x00000003UL

DWORD_PTR APIENTRY NtUserCallTwoParam(DWORD_PTR Param1, DWORD_PTR Param2, DWORD Routine);
