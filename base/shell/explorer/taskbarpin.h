/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Persistent taskbar pin storage helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#define TASKBAR_PIN_CHANGED_MESSAGE L"TaskbarPinningChanged"

typedef BOOL (CALLBACK *TASKBAR_PIN_ENUM_PROC)(PCWSTR pszShortcut,
                                               PCWSTR pszTarget,
                                               const FILETIME *pCreationTime,
                                               LPARAM lParam);

BOOL TaskbarPin_IsPinnable(PCWSTR pszSource, CStringW *pTarget = NULL);
BOOL TaskbarPin_IsDisabled();
BOOL TaskbarPin_ResolveTarget(PCWSTR pszSource, CStringW &Target);
HRESULT TaskbarPin_Find(PCWSTR pszTarget, CStringW *pShortcut = NULL);
HRESULT TaskbarPin_Enum(TASKBAR_PIN_ENUM_PROC pfnCallback, LPARAM lParam);
HRESULT TaskbarPin_Create(PCWSTR pszSource, CStringW *pShortcut = NULL, HICON hIcon = NULL);
HRESULT TaskbarPin_Remove(PCWSTR pszTarget);
HRESULT TaskbarPin_Launch(PCWSTR pszSource, HANDLE *phProcess);
