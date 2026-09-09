# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

@ stdcall DwmProfileGetSnapshotSize()
@ stdcall DwmProfileStartCapture(ptr)
@ stdcall DwmProfileStopCapture(long ptr long)
@ stdcall DwmProfileReadLastCapture(ptr long)
@ stdcall DwmProfileCapture(long ptr long)
@ stdcall DwmPresentationTraceControl(ptr ptr long)
