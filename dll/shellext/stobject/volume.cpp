/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/shellext/stobject/volume.cpp
 * PURPOSE:     Volume notification icon handler
 * PROGRAMMERS: David Quintana <gigaherz@gmail.com>
 */

#include "precomp.h"

#include <mmddk.h>

HICON g_hIconVolume;
HICON g_hIconMute;

HMIXER g_hMixer;
UINT   g_mixerId = (UINT)-1;
DWORD  g_mixerLineID = (DWORD)-1;
DWORD  g_muteControlID = (DWORD)-1;

UINT g_mmDeviceChange;

static BOOL g_IsMute = FALSE;
static DWORD g_volControlID = (DWORD)-1;
static DWORD g_volMinimum = 0;
static DWORD g_volMaximum = 0xFFFF;
static int g_VolLevel = 2;
static int g_VolPercent = 50;
static BOOL g_VolCacheValid = FALSE;
static HICON g_hIconVolume0 = NULL;
static HICON g_hIconVolume1 = NULL;

static HRESULT __stdcall Volume_FindMixerControl(CSysTray * pSysTray)
{
    MMRESULT result;
    UINT mixerId = 0;
    DWORD waveOutId = 0;
    DWORD param2 = 0;

    TRACE("Volume_FindDefaultMixerID\n");

    if (g_hMixer)
    {
        mixerClose(g_hMixer);
        g_hMixer = NULL;
    }
    g_mixerId = (UINT)-1;
    g_mixerLineID = (DWORD)-1;
    g_muteControlID = (DWORD)-1;
    g_volControlID = (DWORD)-1;
    g_VolCacheValid = FALSE;

    result = waveOutMessage((HWAVEOUT)UlongToHandle(WAVE_MAPPER), DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&waveOutId, (DWORD_PTR)&param2);
    if (result)
        return E_FAIL;

    if (waveOutId == (DWORD)-1)
    {
        TRACE("WARNING: waveOut has no default device, trying with first available device...\n", waveOutId);

        mixerId = 0;
    }
    else
    {
        TRACE("waveOut default device is %d\n", waveOutId);

        result = mixerGetID((HMIXEROBJ)UlongToHandle(waveOutId), &mixerId, MIXER_OBJECTF_WAVEOUT);
        if (result)
            return E_FAIL;

        TRACE("mixerId for waveOut default device is %d\n", mixerId);
    }

    g_mixerId = mixerId;

    MIXERCAPS mixerCaps;
    MIXERLINE mixerLine;
    MIXERCONTROL mixerControl;
    MIXERLINECONTROLS mixerLineControls;

    ZeroMemory(&mixerCaps, sizeof(mixerCaps));
    ZeroMemory(&mixerLine, sizeof(mixerLine));
    ZeroMemory(&mixerControl, sizeof(mixerControl));
    ZeroMemory(&mixerLineControls, sizeof(mixerLineControls));

    if (mixerGetDevCapsW(g_mixerId, &mixerCaps, sizeof(mixerCaps)))
        goto OpenMixer;

    if (mixerCaps.cDestinations == 0)
        goto OpenMixer;

    TRACE("mixerCaps.cDestinations %d\n", mixerCaps.cDestinations);

    DWORD idx;
    for (idx = 0; idx < mixerCaps.cDestinations; idx++)
    {
        mixerLine.cbStruct = sizeof(mixerLine);
        mixerLine.dwDestination = idx;
        if (!mixerGetLineInfoW((HMIXEROBJ)UlongToHandle(g_mixerId), &mixerLine, 0))
        {
            if (mixerLine.dwComponentType >= MIXERLINE_COMPONENTTYPE_DST_SPEAKERS &&
                mixerLine.dwComponentType <= MIXERLINE_COMPONENTTYPE_DST_HEADPHONES)
                break;
            TRACE("Destination %d was not speakers or headphones.\n");
        }
    }

    if (idx >= mixerCaps.cDestinations)
        goto OpenMixer;

    TRACE("Valid destination %d found.\n");

    g_mixerLineID = mixerLine.dwLineID;

    mixerLineControls.cbStruct = sizeof(mixerLineControls);
    mixerLineControls.dwLineID = mixerLine.dwLineID;
    mixerLineControls.cControls = 1;
    mixerLineControls.dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
    mixerLineControls.pamxctrl = &mixerControl;
    mixerLineControls.cbmxctrl = sizeof(mixerControl);

    if (!mixerGetLineControlsW((HMIXEROBJ)UlongToHandle(g_mixerId), &mixerLineControls,
                               MIXER_GETLINECONTROLSF_ONEBYTYPE))
    {
        TRACE("Found control id %d for mute\n", mixerControl.dwControlID);
        g_muteControlID = mixerControl.dwControlID;
    }

    g_volControlID = (DWORD)-1;
    ZeroMemory(&mixerControl, sizeof(mixerControl));
    mixerControl.cbStruct = sizeof(mixerControl);
    mixerLineControls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    if (!mixerGetLineControlsW((HMIXEROBJ)UlongToHandle(g_mixerId), &mixerLineControls, MIXER_GETLINECONTROLSF_ONEBYTYPE))
    {
        g_volControlID = mixerControl.dwControlID;
        g_volMinimum = mixerControl.Bounds.dwMinimum;
        g_volMaximum = mixerControl.Bounds.dwMaximum;
        if (g_volMaximum <= g_volMinimum)
        {
            g_volMinimum = 0;
            g_volMaximum = 0xFFFF;
        }
    }

OpenMixer:
    /* Keep the tray cache current without querying the mixer in the click path. */
    mixerOpen(&g_hMixer, g_mixerId, (DWORD_PTR)pSysTray->GetHWnd(), 0, CALLBACK_WINDOW);

    return S_OK;
}

static int Volume_Level()
{
    MIXERCONTROLDETAILS details;
    MIXERCONTROLDETAILS_UNSIGNED value = { 0 };

    if (g_mixerId == (UINT)-1 || g_volControlID == (DWORD)-1)
        return 2;
    ZeroMemory(&details, sizeof(details));
    details.cbStruct = sizeof(details);
    details.hwndOwner = 0;
    details.dwControlID = g_volControlID;
    details.cChannels = 1;
    details.paDetails = &value;
    details.cbDetails = sizeof(value);
    if (mixerGetControlDetailsW((HMIXEROBJ)UlongToHandle(g_mixerId), &details, 0))
        return 2;
    if (value.dwValue <= g_volMinimum)
        g_VolPercent = 0;
    else if (value.dwValue >= g_volMaximum)
        g_VolPercent = 100;
    else
        g_VolPercent = MulDiv(value.dwValue - g_volMinimum, 100,
                              g_volMaximum - g_volMinimum);
    g_VolCacheValid = TRUE;
    if (g_VolPercent == 0)
        return 0;
    if (g_VolPercent < 34)
        return 1;
    return 2;
}

static HICON Volume_PickIcon()
{
    if (g_IsMute)
        return g_hIconMute;
    if (g_VolLevel == 0 && g_hIconVolume0)
        return g_hIconVolume0;
    if (g_VolLevel == 1 && g_hIconVolume1)
        return g_hIconVolume1;
    return g_hIconVolume;
}

HRESULT Volume_IsMute()
{
    MIXERCONTROLDETAILS mixerControlDetails;

    if (g_mixerId != (UINT)-1 && g_muteControlID != (DWORD)-1)
    {
        BOOL detailsResult = 0;
        ZeroMemory(&mixerControlDetails, sizeof(mixerControlDetails));
        mixerControlDetails.cbStruct = sizeof(mixerControlDetails);
        mixerControlDetails.hwndOwner = 0;
        mixerControlDetails.dwControlID = g_muteControlID;
        mixerControlDetails.cChannels = 1;
        mixerControlDetails.paDetails = &detailsResult;
        mixerControlDetails.cbDetails = sizeof(detailsResult);
        if (mixerGetControlDetailsW((HMIXEROBJ)UlongToHandle(g_mixerId), &mixerControlDetails, 0))
            return E_FAIL;

        TRACE("Obtained mute status %d\n", detailsResult);

        g_IsMute = detailsResult != 0;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE Volume_Init(_In_ CSysTray * pSysTray)
{
    HRESULT hr;
    WCHAR strTooltip[128];

    TRACE("Volume_Init\n");

    if (!g_hMixer)
    {
        hr = Volume_FindMixerControl(pSysTray);
        if (FAILED(hr))
            return hr;

        g_mmDeviceChange = RegisterWindowMessageW(L"winmm_devicechange");
    }

    g_hIconVolume = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_VOLUME));
    g_hIconMute = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_VOLMUTE));
    g_hIconVolume0 = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_VOLUME0));
    g_hIconVolume1 = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_VOLUME1));

    Volume_IsMute();
    g_VolLevel = Volume_Level();

    HICON icon = Volume_PickIcon();

    LoadStringW(g_hInstance, IDS_VOL_VOLUME, strTooltip, _countof(strTooltip));
    return pSysTray->NotifyIcon(NIM_ADD, ID_ICON_VOLUME, icon, strTooltip);
}

HRESULT STDMETHODCALLTYPE Volume_Update(_In_ CSysTray * pSysTray)
{
    BOOL PrevState;

    TRACE("Volume_Update\n");

    int PrevLevel = g_VolLevel;
    PrevState = g_IsMute;
    Volume_IsMute();
    g_VolLevel = Volume_Level();

    if (PrevState != g_IsMute || PrevLevel != g_VolLevel)
    {
        WCHAR strTooltip[128];
        HICON icon = Volume_PickIcon();
        LoadStringW(g_hInstance, g_IsMute ? IDS_VOL_MUTED : IDS_VOL_VOLUME, strTooltip, _countof(strTooltip));

        return pSysTray->NotifyIcon(NIM_MODIFY, ID_ICON_VOLUME, icon, strTooltip);
    }
    else
    {
        return S_OK;
    }
}

HRESULT STDMETHODCALLTYPE Volume_Shutdown(_In_ CSysTray * pSysTray)
{
    TRACE("Volume_Shutdown\n");

    if (g_hMixer)
    {
        mixerClose(g_hMixer);
        g_hMixer = NULL;
    }

    return pSysTray->NotifyIcon(NIM_DELETE, ID_ICON_VOLUME, NULL, NULL);
}

HRESULT Volume_OnDeviceChange(_In_ CSysTray * pSysTray, WPARAM wParam, LPARAM lParam)
{
    HRESULT hr = Volume_FindMixerControl(pSysTray);
    if (SUCCEEDED(hr))
        return Volume_Update(pSysTray);
    return hr;
}

static void _RunVolume(BOOL bTray)
{
    ShellExecuteW(NULL,
                  NULL,
                  L"sndvol32.exe",
                  bTray ? L"/t" : NULL,
                  NULL,
                  SW_SHOWNORMAL);
}

static void _RunMMCpl()
{
    CSysTray::RunDll("mmsys.cpl", "");
}

static void _ShowFlyout()
{
    HWND hwndTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    WORD state = (WORD)((g_VolCacheValid ? 0x8000 : 0) | (g_IsMute ? 1 : 0));
    LPARAM cachedState = MAKELPARAM((WORD)g_VolPercent, state);

    if (!hwndTaskbar ||
        !SendMessageW(hwndTaskbar, WM_USER + 270, 0, cachedState))
    {
        _RunVolume(TRUE);
    }
}

static void _ShowContextMenu(CSysTray * pSysTray)
{
    WCHAR strAdjust[128];
    WCHAR strOpen[128];
    LoadStringW(g_hInstance, IDS_VOL_OPEN, strOpen, _countof(strOpen));
    LoadStringW(g_hInstance, IDS_VOL_ADJUST, strAdjust, _countof(strAdjust));

    HMENU hPopup = CreatePopupMenu();
    AppendMenuW(hPopup, MF_STRING, IDS_VOL_OPEN, strOpen);
    AppendMenuW(hPopup, MF_STRING, IDS_VOL_ADJUST, strAdjust);
    SetMenuDefaultItem(hPopup, IDS_VOL_OPEN, FALSE);

    DWORD flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_BOTTOMALIGN;
    POINT pt;
    SetForegroundWindow(pSysTray->GetHWnd());
    GetCursorPos(&pt);

    DWORD id = TrackPopupMenuEx(hPopup, flags,
        pt.x, pt.y,
        pSysTray->GetHWnd(), NULL);

    DestroyMenu(hPopup);

    switch (id)
    {
    case IDS_VOL_OPEN:
        _RunVolume(FALSE);
        break;
    case IDS_VOL_ADJUST:
        _RunMMCpl();
        break;
    }
}

HRESULT STDMETHODCALLTYPE Volume_Message(_In_ CSysTray * pSysTray, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
{
    if (uMsg == g_mmDeviceChange)
        return Volume_OnDeviceChange(pSysTray, wParam, lParam);
    if (uMsg == MM_MIXM_CONTROL_CHANGE)
        return Volume_Update(pSysTray);

    switch (uMsg)
    {
        case WM_USER + 220:
            TRACE("Volume_Message: WM_USER+220\n");
            if (wParam == VOLUME_SERVICE_FLAG)
            {
                if (lParam)
                {
                    pSysTray->EnableService(VOLUME_SERVICE_FLAG, TRUE);
                    return Volume_Init(pSysTray);
                }
                else
                {
                    pSysTray->EnableService(VOLUME_SERVICE_FLAG, FALSE);
                    return Volume_Shutdown(pSysTray);
                }
            }
            return S_FALSE;

        case WM_USER + 221:
            TRACE("Volume_Message: WM_USER+221\n");
            if (wParam == VOLUME_SERVICE_FLAG)
            {
                lResult = (LRESULT)pSysTray->IsServiceEnabled(VOLUME_SERVICE_FLAG);
                return S_OK;
            }
            return S_FALSE;

        case ID_ICON_VOLUME:
            TRACE("Volume_Message uMsg=%d, w=%x, l=%x\n", uMsg, wParam, lParam);

            switch (lParam)
            {
                case WM_LBUTTONDOWN:
                    break;

                case WM_LBUTTONUP:
                    _ShowFlyout();
                    break;

                case WM_LBUTTONDBLCLK:
                    _RunVolume(FALSE);
                    break;

                case WM_RBUTTONDOWN:
                    break;

                case WM_RBUTTONUP:
                    _ShowContextMenu(pSysTray);
                    break;

                case WM_RBUTTONDBLCLK:
                    break;

                case WM_MOUSEMOVE:
                    break;
            }
            return S_OK;

        default:
            TRACE("Volume_Message received for unknown ID %d, ignoring.\n");
            return S_FALSE;
    }

    return S_FALSE;
}
