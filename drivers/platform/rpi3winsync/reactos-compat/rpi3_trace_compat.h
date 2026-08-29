/*
 * PROJECT:     ReactOS Raspberry Pi 3 Windows driver compatibility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Replace WPP-generated trace functions for synchronized drivers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <wpprecorder.h>

#ifndef TRACE_LEVEL_ERROR
#define TRACE_LEVEL_CRITICAL 1
#define TRACE_LEVEL_ERROR 2
#define TRACE_LEVEL_WARNING 3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE 5
#endif

#define TRACE_FLAG_WDFLOADING 0x00000001
#define TRACE_FLAG_SPBDDI 0x00000002
#define TRACE_FLAG_PBCLOADING 0x00000004
#define TRACE_FLAG_TRANSFER 0x00000008
#define TRACE_FLAG_OTHER 0x00000010

#define BSC_TRACING_DEFAULT 0x00000001
#define BSC_TRACING_VERBOSE 0x00000002
#define BSC_TRACING_DEBUG 0x00000004
#define BSC_TRACING_BUGCHECK 0x00000008

#define AUXSPI_TRACING_DEFAULT 0x00000001

#define TraceEvents(_Level, _Flags, _Format, ...) \
    DbgPrint("RPi3: %s\n", (_Format))
#define Trace(_Level, _Flags, _Format, ...) \
    DbgPrint("RPi3: %s\n", (_Format))
#define TraceMessage(_Level, _Flags, _Message) \
    do { DbgPrint _Message; DbgPrint("\n"); } while (0)
#define FuncEntry(...) do { } while (0)
#define FuncExit(...) do { } while (0)

#define BSC_LOG_ERROR(_Format, ...) DbgPrint("BCMI2C: ERROR: %s\n", (_Format))
#define BSC_LOG_LOW_MEMORY(_Format, ...) DbgPrint("BCMI2C: LOW MEMORY: %s\n", (_Format))
#define BSC_LOG_WARNING(_Format, ...) DbgPrint("BCMI2C: WARNING: %s\n", (_Format))
#define BSC_LOG_INFORMATION(_Format, ...) DbgPrint("BCMI2C: INFO: %s\n", (_Format))
#define BSC_LOG_TRACE(_Format, ...) DbgPrint("BCMI2C: TRACE: %s\n", (_Format))

#define AUXSPI_LOG_ERROR(_Format, ...) DbgPrint("BCMAUXSPI: ERROR: %s\n", (_Format))
#define AUXSPI_LOG_LOW_MEMORY(_Format, ...) DbgPrint("BCMAUXSPI: LOW MEMORY: %s\n", (_Format))
#define AUXSPI_LOG_WARNING(_Format, ...) DbgPrint("BCMAUXSPI: WARNING: %s\n", (_Format))
#define AUXSPI_LOG_INFORMATION(_Format, ...) DbgPrint("BCMAUXSPI: INFO: %s\n", (_Format))
#define AUXSPI_LOG_TRACE(_Format, ...) DbgPrint("BCMAUXSPI: TRACE: %s\n", (_Format))

#define PL011_LOG_ASSERTION(_Format, ...) DbgPrint("PL011: ASSERTION: %s\n", (_Format))
#define PL011_LOG_ERROR(_Format, ...) DbgPrint("PL011: ERROR: %s\n", (_Format))
#define PL011_LOG_WARNING(_Format, ...) DbgPrint("PL011: WARNING: %s\n", (_Format))
#define PL011_LOG_INFORMATION(_Format, ...) DbgPrint("PL011: INFO: %s\n", (_Format))
#define PL011_LOG_TRACE(_Format, ...) DbgPrint("PL011: TRACE: %s\n", (_Format))
#define PL011_ASSERT(_Expression) NT_ASSERT(_Expression)

#define SDHC_LOG_CRITICAL_ERROR(_Format, ...) DbgPrint("RPISDHC: CRITICAL: %s\n", (_Format))
#define SDHC_LOG_ASSERTION(_Format, ...) DbgPrint("RPISDHC: ASSERTION: %s\n", (_Format))
#define SDHC_LOG_ERROR(_Format, ...) DbgPrint("RPISDHC: ERROR: %s\n", (_Format))
#define SDHC_LOG_LOW_MEMORY(_Format, ...) DbgPrint("RPISDHC: LOW MEMORY: %s\n", (_Format))
#define SDHC_LOG_WARNING(_Format, ...) DbgPrint("RPISDHC: WARNING: %s\n", (_Format))
#define SDHC_LOG_INFORMATION(_Format, ...) DbgPrint("RPISDHC: INFO: %s\n", (_Format))
#define SDHC_LOG_TRACE(_Format, ...) DbgPrint("RPISDHC: TRACE: %s\n", (_Format))
#define SDHC_CRITICAL_ASSERT(_Expression) NT_ASSERT(_Expression)
#define SDHC_ASSERT(_Expression) NT_ASSERT(_Expression)

#define VCHIQ_LOG_INFORMATION(_Format, ...) \
    DbgPrint("VCHIQ: INFO %s:%d: %s\n", __FILE__, __LINE__, (_Format))
#define VCHIQ_LOG_WARNING(_Format, ...) \
    DbgPrint("VCHIQ: WARNING %s:%d: %s\n", __FILE__, __LINE__, (_Format))
#define VCHIQ_LOG_ERROR(_Format, ...) \
    DbgPrint("VCHIQ: ERROR %s:%d: %s\n", __FILE__, __LINE__, (_Format))
