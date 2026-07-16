/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Versioned recording codec boundary
 */

#pragma once

#include "profiler_model.h"

typedef struct _RPERF_SESSION RPERF_SESSION;

typedef enum _RPERF_CODEC_FORMAT
{
    RperfCodecAuto = 0,
    RperfCodecV1Text = 1,
    RperfCodecV2Binary = 2
} RPERF_CODEC_FORMAT;

typedef VOID (CALLBACK *RPERF_CODEC_PROGRESS)(PVOID Context,
                                               ULONGLONG Completed,
                                               ULONGLONG Total);

BOOL RperfCodecLoad(PCWSTR Path,
                    RPERF_CODEC_FORMAT RequestedFormat,
                    const RPERF_CAPTURE_LIMITS *Limits,
                    HANDLE CancelEvent,
                    RPERF_CODEC_PROGRESS Progress,
                    PVOID ProgressContext,
                    RPERF_RECORDING **Recording,
                    RPERF_CODEC_FORMAT *DetectedFormat);
BOOL RperfCodecSave(PCWSTR Path,
                    RPERF_CODEC_FORMAT Format,
                    const RPERF_RECORDING *Recording,
                    HANDLE CancelEvent,
                    RPERF_CODEC_PROGRESS Progress,
                    PVOID ProgressContext);

/*
 * Incremental v2 stream writer.  Event records are appended to the log while
 * capture is still running; the tables that need the complete recording
 * (strings, session, sources, modules, symbols, image events) are emitted at
 * finalization.  The loader parses chunks by type in fixed passes, so chunk
 * file order does not matter.  Append failures latch inside the stream and
 * surface at finalization so the caller can fall back to a whole-file save.
 */
typedef struct _RPERF_CODEC_STREAM RPERF_CODEC_STREAM;

RPERF_CODEC_STREAM *RperfCodecStreamOpen(PCWSTR Path,
                                         const RPERF_CAPTURE_LIMITS *Limits);
VOID CALLBACK RperfCodecStreamSink(PVOID Context,
                                   const RPERF_RECORD *Record);
BOOL RperfCodecStreamFinalize(RPERF_CODEC_STREAM *Stream,
                              const RPERF_RECORDING *Recording);
VOID RperfCodecStreamAbort(RPERF_CODEC_STREAM *Stream);
RPERF_RECORDING *
RperfRecordingFromLegacySession(const RPERF_SESSION *Legacy,
                                const RPERF_CAPTURE_LIMITS *Limits);
