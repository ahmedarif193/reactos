/*
 * PROJECT:     ReactOS Raspberry Pi 3 Windows driver compatibility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Replace WPP-generated RPIQ trace macros without changing its sources
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#define RPIQ_LOG_INFORMATION(_Format, ...) \
    DbgPrint("RPIQ: INFO %s:%d: %s\n", __FILE__, __LINE__, (_Format))
#define RPIQ_LOG_WARNING(_Format, ...) \
    DbgPrint("RPIQ: WARNING %s:%d: %s\n", __FILE__, __LINE__, (_Format))
#define RPIQ_LOG_ERROR(_Format, ...) \
    DbgPrint("RPIQ: ERROR %s:%d: %s\n", __FILE__, __LINE__, (_Format))
