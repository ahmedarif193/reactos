/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Services page — SCM enumeration, Name/PID/Description/Status/Group columns
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #8.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * ----------------------------------------------------------------------- */
HWND PageServices_Create (HWND hHost, HINSTANCE hInst);
void PageServices_Tick   (HWND hPage);
void PageServices_Resize (HWND hPage, int cx, int cy);
void PageServices_Destroy(HWND hPage);
void PageServices_Theme  (HWND hPage);
