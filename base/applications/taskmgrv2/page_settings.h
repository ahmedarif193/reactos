/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Settings page — theme, refresh interval, default page, options
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, §3.9.1, constraint #12.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * ----------------------------------------------------------------------- */
HWND PageSettings_Create (HWND hHost, HINSTANCE hInst);
void PageSettings_Tick   (HWND hPage);  /* no-op */
void PageSettings_Resize (HWND hPage, int cx, int cy);
void PageSettings_Destroy(HWND hPage);
void PageSettings_Theme  (HWND hPage);

/* -------------------------------------------------------------------------
 * Extra exports (spec §3.9.1) — shell calls these when another subsystem
 * changes a setting so the controls stay in sync.
 * ----------------------------------------------------------------------- */
void PageSettings_OnThemeChanged      (HWND hPage, BOOL bDark);
void PageSettings_OnRefreshChanged    (HWND hPage, DWORD ms);
void PageSettings_OnDefaultPageChanged(HWND hPage, MODERN_PAGE_ID id);
void PageSettings_OnUsersFullNameChanged(HWND hPage, BOOL bShow);
void PageSettings_OnRealtimeWarnChanged (HWND hPage, BOOL bWarn);
