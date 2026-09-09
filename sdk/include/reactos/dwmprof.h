/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * Userspace profiling SDK. Link libdwmprof; runtime dwmprof.dll.
 * ReadLastCapture copies the last stopped capture, including while a new one
 * is recording. Captures are desktop-session scoped and vanish on DWM exit.
 * Inspect Available and Status[]: success does not imply an ICD is installed.
 */
#ifndef ROS_DWM_PROF_H
#define ROS_DWM_PROF_H
#include <windows.h>
#include <reactos/dwmpresenttrace.h>
#ifdef __cplusplus
extern "C" {
#endif
ULONG WINAPI DwmProfileGetSnapshotSize(void);
HRESULT WINAPI DwmProfileStartCapture(ULONG *Session);
HRESULT WINAPI DwmProfileStopCapture(ULONG Session, DPT_SNAPSHOT *Output, ULONG Bytes);
HRESULT WINAPI DwmProfileReadLastCapture(DPT_SNAPSHOT *Output, ULONG Bytes);
HRESULT WINAPI DwmProfileCapture(ULONG Milliseconds, DPT_SNAPSHOT *Output, ULONG Bytes);
HRESULT WINAPI DwmPresentationTraceControl(const DPT_REQUEST *Request, DPT_SNAPSHOT *Output, ULONG Bytes);
#ifdef __cplusplus
}
#endif
#endif
