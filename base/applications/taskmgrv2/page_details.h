/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Details page — flat listview of all processes, right-click context menu
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #7.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * ----------------------------------------------------------------------- */
HWND PageDetails_Create (HWND hHost, HINSTANCE hInst);
void PageDetails_Tick   (HWND hPage);
void PageDetails_Resize (HWND hPage, int cx, int cy);
void PageDetails_Destroy(HWND hPage);
void PageDetails_Theme  (HWND hPage);
