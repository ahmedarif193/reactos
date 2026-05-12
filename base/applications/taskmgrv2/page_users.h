/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Users page — WTSEnumerateSessionsW, columns User/Session/Status/CPU/Memory
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #9.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * ----------------------------------------------------------------------- */
HWND PageUsers_Create (HWND hHost, HINSTANCE hInst);
void PageUsers_Tick   (HWND hPage);
void PageUsers_Resize (HWND hPage, int cx, int cy);
void PageUsers_Destroy(HWND hPage);
void PageUsers_Theme  (HWND hPage);
