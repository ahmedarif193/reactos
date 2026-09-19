/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private USER calls for local session notifications
 */
#pragma once

#define ROS_WTS_REGISTER   0xfffd0053
#define ROS_WTS_UNREGISTER 0xfffd0054
#define ROS_WTS_NOTIFY     0xfffd0055

DWORD_PTR APIENTRY NtUserCallTwoParam(DWORD_PTR Param1, DWORD_PTR Param2, DWORD Routine);
