/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Private, versioned WGL shared-surface bridge. Included after windows.h.
 * The handle is a D3DKMT global share, never a CPU pointer. Binding retains
 * the GPU allocation until the GL texture and queued users release it.
 * The caller must publish completed producer work before drawing and must
 * finish consumer work before acknowledging that publication for reuse. */
#pragma once
#define DWM_WGL_SHARED_TEXTURE_VERSION 1u
#define DWM_WGL_SHARED_BGRA8 87u
typedef BOOL (WINAPI *PFNWGLBINDSHAREDTEXTUREROS)(
    UINT Version, UINT GlobalShare, UINT Width, UINT Height, UINT Pitch, UINT Format);
BOOL WINAPI wglBindSharedTextureROS(
    UINT Version, UINT GlobalShare, UINT Width, UINT Height, UINT Pitch, UINT Format);
/* Notify a completed publication on the currently bound imported texture.
 * Once used, callers must notify every content change before sampling it. */
typedef BOOL (WINAPI *PFNWGLUPDATESHAREDTEXTUREROS)(UINT Version);
BOOL WINAPI wglUpdateSharedTextureROS(UINT Version);
