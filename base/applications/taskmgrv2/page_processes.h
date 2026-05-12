/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Processes page — grouped listview, live CPU/memory/disk/network columns
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #6.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.9)
 * ----------------------------------------------------------------------- */
HWND PageProcesses_Create (HWND hHost, HINSTANCE hInst);
void PageProcesses_Tick   (HWND hPage);
void PageProcesses_Resize (HWND hPage, int cx, int cy);
void PageProcesses_Destroy(HWND hPage);
void PageProcesses_Theme  (HWND hPage);
