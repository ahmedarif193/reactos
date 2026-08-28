/*
 * PROJECT:     ReactOS Raspberry Pi 3 Windows driver compatibility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Replace WPP-generated trace functions for synchronized drivers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <wpprecorder.h>

#define TraceEvents(_Level, _Flags, _Format, ...) \
    DbgPrint("RPi3: %s\n", (_Format))
#define FuncEntry() do { } while (0)
#define FuncExit() do { } while (0)

#define VCHIQ_LOG_INFORMATION(_Format, ...) \
    DbgPrint("VCHIQ: INFO %s:%d: %s\n", __FILE__, __LINE__, (_Format))
#define VCHIQ_LOG_WARNING(_Format, ...) \
    DbgPrint("VCHIQ: WARNING %s:%d: %s\n", __FILE__, __LINE__, (_Format))
#define VCHIQ_LOG_ERROR(_Format, ...) \
    DbgPrint("VCHIQ: ERROR %s:%d: %s\n", __FILE__, __LINE__, (_Format))
