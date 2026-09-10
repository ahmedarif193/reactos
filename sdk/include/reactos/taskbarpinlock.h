/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed Arif
 */

#pragma once

// Serialize Explorer and shell context handlers that write the same user's pin folder.
class CTaskbarPinLock
{
    HANDLE m_Mutex;
    BOOL m_Owned;
    CTaskbarPinLock(const CTaskbarPinLock &);
    CTaskbarPinLock &operator=(const CTaskbarPinLock &);

public:
    CTaskbarPinLock() : m_Mutex(NULL), m_Owned(FALSE) {}

    ~CTaskbarPinLock()
    {
        if (m_Owned)
            ReleaseMutex(m_Mutex);
        if (m_Mutex)
            CloseHandle(m_Mutex);
    }

    HRESULT Acquire(PCWSTR pszFolder)
    {
        WCHAR szFolder[MAX_PATH], szName[64];
        if (!LCMapStringW(LOCALE_INVARIANT, LCMAP_UPPERCASE, pszFolder, -1, szFolder, _countof(szFolder)))
            return HRESULT_FROM_WIN32(GetLastError());
        DWORD hash = 2166136261u;
        for (PCWSTR p = szFolder; *p; ++p)
            hash = (hash ^ *p) * 16777619u;
        HRESULT hr = StringCchPrintfW(szName, _countof(szName), L"Local\\ReactOS.TaskbarPins.%08lx", hash);
        if (FAILED(hr))
            return hr;
        m_Mutex = CreateMutexW(NULL, FALSE, szName);
        if (!m_Mutex)
            return HRESULT_FROM_WIN32(GetLastError());
        DWORD wait = WaitForSingleObject(m_Mutex, INFINITE);
        m_Owned = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        return m_Owned ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
};
