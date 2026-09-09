/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#pragma once
#include <reactos/dwmpresenttracecore.h>
extern DPT_BANK g_DwmPresentTrace;
void DwmPresentTraceSetIcd(PFNWGLCONTROLPRESENTATIONTRACEROS Control);
