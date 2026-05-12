/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Startup page — Run registry keys + Startup folder enumeration
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #10.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * ----------------------------------------------------------------------- */
HWND PageStartup_Create (HWND hHost, HINSTANCE hInst);
void PageStartup_Tick   (HWND hPage);
void PageStartup_Resize (HWND hPage, int cx, int cy);
void PageStartup_Destroy(HWND hPage);
void PageStartup_Theme  (HWND hPage);
