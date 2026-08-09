/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Generic sensors and thermal telemetry page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "app.h"

#define PGSENSOR_CLASS L"TM11PageSensors"
#define ELECTRICAL_CARD_MAX 12
#define POWER_CARD_MAX 12

enum
{
    SNC_NAME = 0,
    SNC_VALUE,
    SNC_TYPE,
    SNC_SOURCE,
    SNC_LIMITS,
    SNC_STATUS
};

struct SensorsPage : Page, ITreeListOwner
{
    HWND tl;
    int sortCol;
    BOOL sortDesc;
    Vec<TLRow> rows;
    Vec<TelemetryRow*> list;
    Vec<TelemetryRow*> electricalCards;
    Vec<TelemetryRow*> powerCards;

    SensorsPage() : tl(NULL), sortCol(SNC_NAME), sortDesc(FALSE) {}

    const WCHAR* Title() { return L"Sensors, thermal & power"; }
    BOOL WantSearch() { return TRUE; }
    const WCHAR* SearchHint() { return L"Search sensors, batteries, value types, or sources"; }

    static TelemetryRow* Sensor(LPARAM data) { return (TelemetryRow*)data; }

    BOOL MatchesSearch(const TelemetryRow& sensor)
    {
        if (!g_app.search[0]) return TRUE;
        return StrStrIW(sensor.name, g_app.search) || StrStrIW(sensor.type, g_app.search) || StrStrIW(sensor.source, g_app.search) || StrStrIW(sensor.limitsText, g_app.search) || StrStrIW(sensor.status, g_app.search);
    }

    struct SortCtx { int col; BOOL desc; };
    static SortCtx s_sc;

    static int CompareAvailable(const TelemetryRow* first, const TelemetryRow* second)
    {
        if (first->available == second->available) return 0;
        return first->available ? -1 : 1;
    }

    static int __cdecl Cmp(const void* firstPtr, const void* secondPtr)
    {
        const TelemetryRow* first = *(const TelemetryRow* const*)firstPtr;
        const TelemetryRow* second = *(const TelemetryRow* const*)secondPtr;
        int result = 0;

        switch (s_sc.col)
        {
        case SNC_VALUE:
            result = CompareAvailable(first, second);
            if (!result && first->numeric && second->numeric)
                result = first->value < second->value ? -1 : first->value > second->value ? 1 : 0;
            else if (!result)
                result = lstrcmpiW(first->valueText, second->valueText);
            break;
        case SNC_TYPE: result = lstrcmpiW(first->type, second->type); break;
        case SNC_SOURCE: result = lstrcmpiW(first->source, second->source); break;
        case SNC_LIMITS: result = lstrcmpiW(first->limitsText, second->limitsText); break;
        case SNC_STATUS: result = lstrcmpiW(first->status, second->status); break;
        default: result = lstrcmpiW(first->name, second->name); break;
        }
        if (!result) result = lstrcmpiW(first->name, second->name);
        return s_sc.desc ? -result : result;
    }

    static LONGLONG StableKey(const TelemetryRow& sensor)
    {
        const ULONG* guidWords = (const ULONG*)&sensor.sensorId;
        const ULONG* formatWords = (const ULONG*)&sensor.fieldFormat;
        ULONGLONG hash = 1469598103934665603ULL;

        for (int i = 0; i < 4; i++) hash = (hash ^ guidWords[i]) * 1099511628211ULL;
        for (int i = 0; i < 4; i++) hash = (hash ^ formatWords[i]) * 1099511628211ULL;
        hash = (hash ^ sensor.fieldId) * 1099511628211ULL;
        hash = (hash ^ sensor.instance) * 1099511628211ULL;
        hash = (hash ^ sensor.sourceKind) * 1099511628211ULL;
        return (LONGLONG)(hash ? hash : 1);
    }

    static BOOL IsBatteryCardField(ULONG fieldId)
    {
        return fieldId == BATTERY_FIELD_STATE || fieldId == BATTERY_FIELD_CHARGE_PERCENT || fieldId == BATTERY_FIELD_REMAINING_CAPACITY || fieldId == BATTERY_FIELD_HEALTH || fieldId == BATTERY_FIELD_VOLTAGE || fieldId == BATTERY_FIELD_POWER || fieldId == BATTERY_FIELD_CURRENT || fieldId == BATTERY_FIELD_ESTIMATED_TIME;
    }

    int CardColumns(int count, int width)
    {
        int columns;

        if (!count) return 0;
        columns = (width - S(24) + S(8)) / (S(144) + S(8));
        if (columns < 1) columns = 1;
        if (columns > count) columns = count;
        return columns;
    }

    int CardSectionHeight(int count, int width)
    {
        int columns = CardColumns(count, width);
        int rowCount;

        if (!columns) return 0;
        rowCount = (count + columns - 1) / columns;
        return S(12) + S(22) + S(8) + rowCount * S(68) + (rowCount - 1) * S(8) + S(12);
    }

    int SummaryPanelHeight(int width)
    {
        return CardSectionHeight(powerCards.n, width) + CardSectionHeight(electricalCards.n, width);
    }

    void Layout()
    {
        RECT client;
        int panelHeight;

        if (!hwnd || !tl) return;
        GetClientRect(hwnd, &client);
        panelHeight = SummaryPanelHeight(client.right - client.left);
        MoveWindow(tl, 0, panelHeight, client.right - client.left, max(0, client.bottom - client.top - panelHeight), TRUE);
    }

    int PaintCardSection(HDC dc, const Vec<TelemetryRow*>& cards, PCWSTR sectionTitle, int top, int width)
    {
        RECT title;
        int columns;
        int innerWidth = width - S(24);
        int gap = S(8);
        int cardHeight = S(68);
        int cardTop;

        if (!cards.n) return top;
        columns = CardColumns(cards.n, width);
        title.left = S(12);
        title.top = top + S(12);
        title.right = width - S(12);
        title.bottom = title.top + S(22);
        DrawTextClip(dc, sectionTitle, title, g_t.fBodySemi, g_t.textMain, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        cardTop = title.bottom + S(8);

        for (int index = 0; index < cards.n; index++)
        {
            TelemetryRow* telemetry = cards[index];
            int column = index % columns;
            int row = index / columns;
            RECT card;
            RECT label;
            RECT value;
            WCHAR valueText[96];
            COLORREF valueColor = telemetry->available ? g_t.textMain : g_t.textDis;

            if (lstrcmpW(telemetry->status, L"Critical") == 0 || lstrcmpW(telemetry->status, L"Error") == 0) valueColor = g_t.dangerText;

            card.left = S(12) + MulDiv(innerWidth + gap, column, columns);
            card.right = S(12) + MulDiv(innerWidth + gap, column + 1, columns) - gap;
            card.top = cardTop + row * (cardHeight + gap);
            card.bottom = card.top + cardHeight;
            FillRoundRect(dc, card, g_t.cardBg, g_t.cardBorder, S(6));
            label.left = card.left + S(12);
            label.top = card.top + S(7);
            label.right = card.right - S(12);
            label.bottom = card.top + S(29);
            DrawTextClip(dc, telemetry->name, label, g_t.fSmallSemi, g_t.textSec, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            if (telemetry->available && telemetry->unit[0]) StringCchPrintfW(valueText, _countof(valueText), L"%s %s", telemetry->valueText, telemetry->unit);
            else StringCchCopyW(valueText, _countof(valueText), telemetry->valueText);
            value.left = card.left + S(12);
            value.top = card.top + S(27);
            value.right = card.right - S(12);
            value.bottom = card.bottom - S(7);
            DrawTextClip(dc, valueText, value, g_t.fMedSemi, valueColor, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        return top + CardSectionHeight(cards.n, width);
    }

    void PaintSummaryPanel(HDC dc, const RECT& paintRect)
    {
        RECT client;
        int top = 0;

        FillRect32(dc, paintRect, g_t.listBg);
        GetClientRect(hwnd, &client);
        top = PaintCardSection(dc, powerCards, L"Power & battery", top, client.right - client.left);
        PaintCardSection(dc, electricalCards, L"Electrical telemetry", top, client.right - client.left);
    }

    void Rebuild()
    {
        Vec<TelemetryRow>& telemetry = Data::Telemetry();
        rows.Clear();
        list.Clear();
        electricalCards.Clear();
        powerCards.Clear();
        BOOL haveBattery = FALSE;

        for (int i = 0; i < telemetry.n; i++)
            if (telemetry[i].sourceKind == TEL_SOURCE_BATTERY)
                haveBattery = TRUE;
        for (int i = 0; i < telemetry.n; i++)
        {
            if (telemetry[i].sourceKind == TEL_SOURCE_BATTERY && IsBatteryCardField(telemetry[i].fieldId) && powerCards.n < POWER_CARD_MAX) powerCards.Push(&telemetry[i]);
            else if (!haveBattery && telemetry[i].sourceKind == TEL_SOURCE_SYSTEM_POWER && powerCards.n < POWER_CARD_MAX) powerCards.Push(&telemetry[i]);
            if (telemetry[i].sourceKind != TEL_SOURCE_BATTERY && (telemetry[i].kind == TEL_VOLTAGE || telemetry[i].kind == TEL_CURRENT || telemetry[i].kind == TEL_POWER || telemetry[i].kind == TEL_ELECTRICAL_CUSTOM) && electricalCards.n < ELECTRICAL_CARD_MAX) electricalCards.Push(&telemetry[i]);
            if (MatchesSearch(telemetry[i]))
                list.Push(&telemetry[i]);
        }

        s_sc.col = sortCol;
        s_sc.desc = sortDesc;
        if (list.n > 1)
            qsort(list.p, list.n, sizeof(TelemetryRow*), Cmp);

        for (int i = 0; i < list.n; i++)
        {
            TLRow* row = rows.Add();
            if (!row) break;
            row->key = StableKey(*list[i]);
            row->data = (LPARAM)list[i];
            row->kind = TLR_ITEM;
        }
        TL_SetRows(tl, rows.p, rows.n);
        Layout();
        InvalidateRect(hwnd, NULL, FALSE);
    }

    void TLCellText(LPARAM data, int col, WCHAR* text, int cch)
    {
        TelemetryRow* sensor = Sensor(data);
        text[0] = 0;
        if (!sensor) return;

        switch (col)
        {
        case SNC_NAME:
            StringCchCopyW(text, cch, sensor->name);
            break;
        case SNC_VALUE:
            if (sensor->available && sensor->unit[0]) StringCchPrintfW(text, cch, L"%s %s", sensor->valueText, sensor->unit);
            else StringCchCopyW(text, cch, sensor->valueText);
            break;
        case SNC_TYPE:
            StringCchCopyW(text, cch, sensor->type);
            break;
        case SNC_SOURCE:
            StringCchCopyW(text, cch, sensor->source);
            break;
        case SNC_LIMITS:
            StringCchCopyW(text, cch, sensor->limitsText);
            break;
        case SNC_STATUS:
            StringCchCopyW(text, cch, sensor->status);
            break;
        }
    }

    IconId TLRowGlyph(LPARAM data)
    {
        (void)data;
        return IC_SENSORS;
    }

    IconId TLStatusGlyph(LPARAM data, int col, COLORREF* color)
    {
        TelemetryRow* sensor = Sensor(data);
        if (!sensor || col != SNC_STATUS) return IC_NONE;
        if (lstrcmpW(sensor->status, L"Normal") == 0 || lstrcmpW(sensor->status, L"Ready") == 0)
        {
            *color = g_t.accent;
            return IC_CHECK;
        }
        if (lstrcmpW(sensor->status, L"Critical") == 0 || lstrcmpW(sensor->status, L"Error") == 0)
        {
            *color = g_t.dangerText;
            return IC_DISABLE;
        }
        return IC_NONE;
    }

    void TLOnSort(int col, BOOL desc)
    {
        sortCol = col;
        sortDesc = desc;
        Rebuild();
    }

    void OnTick() { Data::RefreshTelemetry(); Rebuild(); }
    void OnShow(BOOL shown) { if (shown) { Data::RefreshTelemetry(); Rebuild(); } }
    void OnSearch() { Rebuild(); }
    void OnThemeChanged() { Layout(); TL_Refresh(tl); InvalidateRect(hwnd, NULL, FALSE); }

    HWND Create(HWND parent);
};

SensorsPage::SortCtx SensorsPage::s_sc;
static SensorsPage* s_page;

static LRESULT CALLBACK SensorsPageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (s_page) s_page->Layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        if (s_page) s_page->PaintSummaryPanel(dc, paint.rcPaint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND SensorsPage::Create(HWND parent)
{
    WNDCLASSW windowClass;
    TLColumn columns[6];

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.lpfnWndProc = SensorsPageProc;
    windowClass.hInstance = g_app.hInst;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.lpszClassName = PGSENSOR_CLASS;
    RegisterClassW(&windowClass);

    hwnd = CreateWindowExW(0, PGSENSOR_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, FALSE);
    ZeroMemory(columns, sizeof(columns));
    columns[0].id = SNC_NAME; StringCchCopyW(columns[0].title, _countof(columns[0].title), L"Sensor"); columns[0].width = S(250); columns[0].minWidth = S(150);
    columns[1].id = SNC_VALUE; StringCchCopyW(columns[1].title, _countof(columns[1].title), L"Value"); columns[1].width = S(130); columns[1].minWidth = S(90); columns[1].flags = TLC_RIGHT;
    columns[2].id = SNC_TYPE; StringCchCopyW(columns[2].title, _countof(columns[2].title), L"Type"); columns[2].width = S(160); columns[2].minWidth = S(100);
    columns[3].id = SNC_SOURCE; StringCchCopyW(columns[3].title, _countof(columns[3].title), L"Source"); columns[3].width = S(220); columns[3].minWidth = S(130);
    columns[4].id = SNC_LIMITS; StringCchCopyW(columns[4].title, _countof(columns[4].title), L"Limits"); columns[4].width = S(180); columns[4].minWidth = S(120);
    columns[5].id = SNC_STATUS; StringCchCopyW(columns[5].title, _countof(columns[5].title), L"Status"); columns[5].width = S(120); columns[5].minWidth = S(90);
    TL_SetColumns(tl, columns, _countof(columns));
    TL_SetSort(tl, SNC_NAME, FALSE);
    TL_SetEmptyText(tl, L"No battery, Sensor API provider, ACPI thermal or fan interface, or storage temperature source is available");
    Rebuild();
    return hwnd;
}

Page* CreateSensorsPage(void)
{
    s_page = new SensorsPage();
    return s_page;
}
