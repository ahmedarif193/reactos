/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     App history page — placeholder (not available on ReactOS)
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #11, decision #5.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * Tick is NULL — static placeholder, no data fetch.
 * ----------------------------------------------------------------------- */
HWND PageAppHistory_Create (HWND hHost, HINSTANCE hInst);
void PageAppHistory_Tick   (HWND hPage);  /* no-op */
void PageAppHistory_Resize (HWND hPage, int cx, int cy);
void PageAppHistory_Destroy(HWND hPage);
void PageAppHistory_Theme  (HWND hPage);
