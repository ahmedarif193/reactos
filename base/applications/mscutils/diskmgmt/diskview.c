/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/diskview.c
 * PURPOSE:     Owner-drawn disk map helpers for the Disk Management UI
 */

#include "precomp.h"

static INT
DmDiskViewGetLineHeight(
    _In_ HDC hdc)
{
    TEXTMETRICW tm;

    if (GetTextMetricsW(hdc, &tm))
        return tm.tmHeight + tm.tmExternalLeading + 2;

    return 18;
}

static INT
DmDiskViewPaintLegendItem(
    _In_ HDC hdc,
    _In_ INT X,
    _In_ INT Y,
    _In_ COLORREF Color,
    _In_z_ PCWSTR Text)
{
    RECT BoxRect;
    RECT TextRect;
    HBRUSH Brush;
    SIZE TextSize;

    SetRect(&BoxRect, X, Y, X + 14, Y + 14);
    Brush = CreateSolidBrush(Color);
    FillRect(hdc, &BoxRect, Brush);
    FrameRect(hdc, &BoxRect, GetStockObject(BLACK_BRUSH));
    DeleteObject(Brush);

    TextSize.cx = 120;
    TextSize.cy = 16;
    GetTextExtentPoint32W(hdc, Text, lstrlenW(Text), &TextSize);

    SetRect(&TextRect, BoxRect.right + 6, Y - 1, BoxRect.right + 6 + TextSize.cx + 4, Y + 16);
    DrawTextW(hdc,
              Text,
              -1,
              &TextRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Advance to the next legend item. */
    return 14 + 6 + TextSize.cx + 20;
}

static VOID
DmDiskViewDrawInsetPanel(
    _In_ HDC hdc,
    _In_ const RECT *Rect,
    _In_ INT SysColor)
{
    if (hdc == NULL || Rect == NULL || Rect->right <= Rect->left || Rect->bottom <= Rect->top)
        return;

    FillRect(hdc, Rect, GetSysColorBrush(SysColor));
    DrawEdge(hdc, (LPRECT)Rect, EDGE_SUNKEN, BF_RECT);
}

static VOID
DmDiskViewDrawLabelPanel(
    _In_ HDC hdc,
    _In_ const RECT *Rect)
{
    if (hdc == NULL || Rect == NULL || Rect->right <= Rect->left || Rect->bottom <= Rect->top)
        return;

    FillRect(hdc, Rect, GetSysColorBrush(COLOR_BTNFACE));
    FrameRect(hdc, Rect, GetSysColorBrush(COLOR_3DSHADOW));
}

static COLORREF
DmDiskViewGetRegionBarColor(
    _In_ const DM_REGION *Region)
{
    WCHAR TypeText[96];

    if (Region == NULL || Region->Type == DmRegionFree)
    {
        /* Free space inside extended partition: bright green */
        if (Region != NULL && Region->IsLogical)
            return RGB(0, 192, 0);
        /* Unallocated: black */
        return RGB(0, 0, 0);
    }

    /* Extended partition container: dark green */
    if (Region->IsContainer)
        return RGB(0, 128, 0);

    /* Logical drive: teal */
    if (Region->IsLogical)
        return RGB(0, 128, 128);

    /* GPT special types */
    if (Region->PartitionStyle == PARTITION_STYLE_GPT)
    {
        DmGetGptTypeLabel(&Region->Data.Gpt.PartitionType, TypeText, ARRAYSIZE(TypeText));
        if (_wcsicmp(TypeText, L"EFI System Partition") == 0)
            return RGB(128, 0, 128);   /* EFI system: purple */
        if (_wcsicmp(TypeText, L"Microsoft Reserved Partition") == 0)
            return RGB(128, 128, 0);   /* MSR: olive */
        if (_wcsicmp(TypeText, L"Recovery Partition") == 0)
            return RGB(160, 0, 0);     /* Recovery: dark red */
    }

    /* Dynamic/LDM volume */
    if (Region->IsDynamic)
        return RGB(128, 128, 0);       /* Dynamic: olive */

    /* Primary partition: dark blue (default) */
    return RGB(0, 0, 160);
}

static VOID
DmDiskViewPaintLegend(
    _In_ HDC hdc,
    _In_ const RECT *Rect)
{
    RECT LineRect;
    INT X;
    INT Y;

    if (Rect == NULL || Rect->bottom <= Rect->top)
        return;

    FillRect(hdc, Rect, GetSysColorBrush(COLOR_BTNFACE));
    LineRect = *Rect;
    LineRect.bottom = LineRect.top + 1;
    FillRect(hdc, &LineRect, GetSysColorBrush(COLOR_3DSHADOW));

    X = Rect->left + 8;
    Y = Rect->top + 5;

    X += DmDiskViewPaintLegendItem(hdc, X, Y, RGB(0, 0, 0),     L"Unallocated");
    X += DmDiskViewPaintLegendItem(hdc, X, Y, RGB(0, 0, 160),   L"Primary partition");
    X += DmDiskViewPaintLegendItem(hdc, X, Y, RGB(0, 128, 0),   L"Extended partition");
    X += DmDiskViewPaintLegendItem(hdc, X, Y, RGB(0, 128, 128), L"Logical drive");
    DmDiskViewPaintLegendItem(hdc, X, Y, RGB(0, 192, 0),   L"Free space");
}

static VOID
DmDiskViewBuildDiskKind(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Disk == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Disk->IsDynamic)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Dynamic");
        return;
    }

    if (Disk->IsRemovable)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Removable");
        return;
    }

    if (Disk->PartitionStyle == PARTITION_STYLE_RAW)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Unknown");
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"Basic");
}

static VOID
DmDiskViewBuildRegionRole(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR TypeText[64];

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Region == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Region->Type == DmRegionFree)
    {
        StringCchCopyW(Buffer,
                       cchBuffer,
                       Region->IsLogical ? L"Free Space" : L"Unallocated");
        return;
    }

    if (Region->PartitionStyle == PARTITION_STYLE_GPT)
    {
        DmGetGptTypeLabel(&Region->Data.Gpt.PartitionType, TypeText, ARRAYSIZE(TypeText));
        if (_wcsicmp(TypeText, L"Basic Data Partition") == 0 ||
            _wcsicmp(TypeText, L"Unknown") == 0)
        {
            StringCchCopyW(Buffer, cchBuffer, L"Primary Partition");
            return;
        }

        StringCchCopyW(Buffer, cchBuffer, TypeText);
        return;
    }

    if (Region->PartitionStyle == PARTITION_STYLE_MBR)
    {
        if (Region->IsDynamic && !Region->IsContainer)
        {
            DmGetMbrTypeLabel(Region->Data.Mbr.PartitionType, Buffer, cchBuffer);
            return;
        }

        DmGetMbrRoleLabel(Region->IsLogical,
                          Region->IsContainer,
                          Buffer,
                          cchBuffer);
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"Partition");
}

static VOID
DmDiskViewBuildRegionStatus(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR RoleText[96];
    BOOL First;

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Region == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Region->Type == DmRegionFree)
    {
        StringCchCopyW(Buffer,
                       cchBuffer,
                       Region->IsLogical ? L"Free Space" : L"Unallocated");
        return;
    }

    DmDiskViewBuildRegionRole(Region, RoleText, ARRAYSIZE(RoleText));
    if (_wcsicmp(RoleText, L"EFI System Partition") == 0 ||
        _wcsicmp(RoleText, L"Recovery Partition") == 0 ||
        _wcsicmp(RoleText, L"Microsoft Reserved Partition") == 0)
    {
        StringCchPrintfW(Buffer, cchBuffer, L"Healthy (%s)", RoleText);
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"Healthy");
    if (!Region->IsBoot &&
        !Region->IsSystem &&
        (Region->Volume == NULL || !Region->Volume->IsMultiExtent) &&
        (Region->Volume == NULL || !Region->Volume->IsDynamic) &&
        !Region->IsDynamic)
    {
        StringCchPrintfW(Buffer, cchBuffer, L"Healthy (%s)", RoleText);
        return;
    }

    StringCchCatW(Buffer, cchBuffer, L" (");
    First = TRUE;

    if (Region->IsSystem)
    {
        if (!First)
            StringCchCatW(Buffer, cchBuffer, L", ");
        StringCchCatW(Buffer, cchBuffer, L"System");
        First = FALSE;
    }

    if (Region->IsBoot)
    {
        if (!First)
            StringCchCatW(Buffer, cchBuffer, L", ");
        StringCchCatW(Buffer, cchBuffer, L"Boot");
        First = FALSE;
    }

    if (Region->Volume != NULL && Region->Volume->IsMultiExtent)
    {
        if (!First)
            StringCchCatW(Buffer, cchBuffer, L", ");
        StringCchCatW(Buffer, cchBuffer, L"Multi-extent Volume");
        First = FALSE;
    }

    if (Region->IsDynamic || (Region->Volume != NULL && Region->Volume->IsDynamic))
    {
        if (!First)
            StringCchCatW(Buffer, cchBuffer, L", ");
        StringCchCatW(Buffer, cchBuffer, L"Dynamic Volume");
        First = FALSE;
    }

    if (RoleText[0] != UNICODE_NULL)
    {
        if (!First)
            StringCchCatW(Buffer, cchBuffer, L", ");
        StringCchCatW(Buffer, cchBuffer, RoleText);
    }

    StringCchCatW(Buffer, cchBuffer, L")");
}

static BOOL
DmDiskViewIsSpecialRegion(
    _In_ const DM_REGION *Region)
{
    WCHAR RoleText[96];

    if (Region == NULL || Region->Type != DmRegionPartition)
        return FALSE;

    DmDiskViewBuildRegionRole(Region, RoleText, ARRAYSIZE(RoleText));
    return (_wcsicmp(RoleText, L"Primary Partition") != 0 &&
            _wcsicmp(RoleText, L"Logical Drive") != 0 &&
            _wcsicmp(RoleText, L"Partition") != 0);
}

static VOID
DmDiskViewFillHatch(
    _In_ HDC hdc,
    _In_ const RECT *Rect)
{
    HBRUSH HatchBrush;
    COLORREF OldBk;
    COLORREF OldText;

    HatchBrush = CreateHatchBrush(HS_BDIAGONAL, RGB(205, 205, 205));
    OldBk = SetBkColor(hdc, RGB(255, 255, 255));
    OldText = SetTextColor(hdc, RGB(205, 205, 205));
    FillRect(hdc, Rect, HatchBrush);
    SetBkColor(hdc, OldBk);
    SetTextColor(hdc, OldText);
    DeleteObject(HatchBrush);
}

static VOID
DmDiskViewDrawRegion(
    _In_ HDC hdc,
    _In_ const RECT *Rect,
    _In_ const DM_REGION *Region,
    _In_ BOOL Selected,
    _In_opt_ HFONT Font,
    _In_opt_ HFONT BoldFont)
{
    RECT BarRect;
    RECT TextRect;
    RECT FillRectArea;
    HBRUSH FillBrush;
    HBRUSH BarBrush;
    WCHAR Title[96];
    WCHAR Detail[96];
    WCHAR Status[128];
    HGDIOBJ OldFont;
    BOOL HasTitle;
    INT LineHeight;

    FillBrush = CreateSolidBrush(RGB(255, 255, 255));
    BarBrush = CreateSolidBrush(DmDiskViewGetRegionBarColor(Region));

    FillRect(hdc, Rect, FillBrush);
    FrameRect(hdc, Rect, GetStockObject(BLACK_BRUSH));
    if (Selected)
    {
        RECT InnerRect;

        InnerRect = *Rect;
        InflateRect(&InnerRect, -1, -1);
        FrameRect(hdc, &InnerRect, GetSysColorBrush(COLOR_3DSHADOW));
    }

    BarRect = *Rect;
    BarRect.bottom = min(BarRect.top + DM_DISKVIEW_BAR_HEIGHT, Rect->bottom);
    FillRect(hdc, &BarRect, BarBrush);

    FillRectArea = *Rect;
    FillRectArea.top = BarRect.bottom;
    InflateRect(&FillRectArea, -1, -1);
    if (Region->Type == DmRegionFree)
        DmDiskViewFillHatch(hdc, &FillRectArea);
    else if (Selected)
        DmDiskViewFillHatch(hdc, &FillRectArea);

    TextRect = *Rect;
    InflateRect(&TextRect, -6, -8);
    TextRect.top = BarRect.bottom + 6;

    DmDiskViewBuildRegionTitle(Region, Title, ARRAYSIZE(Title));
    DmDiskViewBuildRegionDetail(Region, Detail, ARRAYSIZE(Detail));
    DmDiskViewBuildRegionStatus(Region, Status, ARRAYSIZE(Status));
    HasTitle = (Title[0] != UNICODE_NULL);

    if (Font == NULL)
        Font = GetStockObject(DEFAULT_GUI_FONT);
    if (BoldFont == NULL)
        BoldFont = Font;

    OldFont = SelectObject(hdc, Font);
    LineHeight = DmDiskViewGetLineHeight(hdc);
    if (HasTitle)
    {
        SelectObject(hdc, BoldFont);
        DrawTextW(hdc,
                  Title,
                  -1,
                  &TextRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, Font);
        TextRect.top += LineHeight;
    }

    DrawTextW(hdc,
              Detail,
              -1,
              &TextRect,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (Status[0] != UNICODE_NULL)
    {
        TextRect.top += LineHeight;
        DrawTextW(hdc,
                  Status,
                  -1,
                  &TextRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, OldFont);

    DeleteObject(BarBrush);
    DeleteObject(FillBrush);
}

static BOOL
DmDiskViewDiskContainsVolume(
    _In_ const DM_DISK *Disk,
    _In_ const DM_VOLUME *Volume)
{
    ULONG Index;

    if (Disk == NULL || Volume == NULL)
        return FALSE;

    for (Index = 0; Index < Disk->RegionCount; Index++)
    {
        if (Disk->Regions[Index].Volume == Volume)
            return TRUE;
    }

    return FALSE;
}

VOID
DmDiskViewPaintEmptyState(
    _In_ HDC hdc,
    _In_ const RECT *ClientRect,
    _In_opt_ HFONT Font)
{
    RECT rc;
    RECT ContentRect;
    RECT LegendRect;
    RECT LabelRect;
    RECT GraphOuterRect;
    RECT GraphRect;
    RECT MessageRect;
    HGDIOBJ OldFont;

    if (hdc == NULL || ClientRect == NULL)
        return;

    rc = *ClientRect;
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

    ContentRect = rc;
    LegendRect = rc;
    LegendRect.top = max(rc.bottom - DM_DISKVIEW_LEGEND_HEIGHT, rc.top);
    ContentRect.bottom = max(LegendRect.top - 1, ContentRect.top);

    if (Font == NULL)
        Font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    SetBkMode(hdc, TRANSPARENT);
    OldFont = SelectObject(hdc, Font);

    LabelRect.left = 6;
    LabelRect.top = DM_DISKVIEW_MARGIN_TOP;
    LabelRect.right = min(LabelRect.left + DM_DISKVIEW_LABEL_WIDTH, ContentRect.right - 10);
    LabelRect.bottom = min(LabelRect.top + DM_DISKVIEW_DEFAULT_ROW_HEIGHT, ContentRect.bottom - 6);
    if (LabelRect.bottom < LabelRect.top)
        LabelRect.bottom = LabelRect.top;

    GraphOuterRect.left = min(LabelRect.right + 6, ContentRect.right - 6);
    GraphOuterRect.top = LabelRect.top;
    GraphOuterRect.right = ContentRect.right - 6;
    GraphOuterRect.bottom = LabelRect.bottom;

    DmDiskViewDrawLabelPanel(hdc, &LabelRect);
    DmDiskViewDrawInsetPanel(hdc, &GraphOuterRect, COLOR_BTNFACE);

    GraphRect = GraphOuterRect;
    InflateRect(&GraphRect, -2, -2);
    FillRect(hdc, &GraphRect, GetSysColorBrush(COLOR_WINDOW));

    MessageRect = GraphRect;
    InflateRect(&MessageRect, -8, -8);
    DrawTextW(hdc,
              L"No disks were detected.",
              -1,
              &MessageRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DmDiskViewPaintLegend(hdc, &LegendRect);
    SelectObject(hdc, OldFont);
}

static VOID
DmDiskViewDrawDiskLabel(
    _In_ HDC hdc,
    _In_ const RECT *Rect,
    _In_ const DM_DISK *Disk,
    _In_ BOOL Selected,
    _In_opt_ HFONT Font,
    _In_opt_ HFONT BoldFont)
{
    RECT TextRect;
    WCHAR Line1[32];
    WCHAR Line2[48];
    WCHAR Line3[48];
    WCHAR Line4[48];
    HGDIOBJ OldFont;
    INT LineHeight;

    UNREFERENCED_PARAMETER(Selected);

    DmDiskViewDrawLabelPanel(hdc, Rect);

    TextRect = *Rect;
    InflateRect(&TextRect, -8, -6);

    StringCchPrintfW(Line1, ARRAYSIZE(Line1), L"Disk %lu", Disk->DiskNumber);
    DmDiskViewBuildDiskSummary(Disk, Line2, ARRAYSIZE(Line2));
    DmDiskViewBuildDiskStatus(Disk, Line3, ARRAYSIZE(Line3));
    DmDiskViewFormatSize(Disk->Size, Line4, ARRAYSIZE(Line4));

    if (Font == NULL)
        Font = GetStockObject(DEFAULT_GUI_FONT);
    if (BoldFont == NULL)
        BoldFont = Font;

    OldFont = SelectObject(hdc, BoldFont);
    LineHeight = DmDiskViewGetLineHeight(hdc);
    DrawTextW(hdc, Line1, -1, &TextRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
    SelectObject(hdc, Font);
    TextRect.top += LineHeight;
    DrawTextW(hdc, Line2, -1, &TextRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    TextRect.top += LineHeight;
    DrawTextW(hdc, Line4, -1, &TextRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    TextRect.top += LineHeight;
    DrawTextW(hdc, Line3, -1, &TextRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, OldFont);
}

VOID
DmDiskViewFormatSize(
    _In_ ULONGLONG Size,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    static const struct
    {
        ULONGLONG Scale;
        PCWSTR Suffix;
    } Units[] =
    {
        { 1024ULL * 1024ULL * 1024ULL * 1024ULL, L"TB" },
        { 1024ULL * 1024ULL * 1024ULL, L"GB" },
        { 1024ULL * 1024ULL, L"MB" },
        { 1024ULL, L"KB" }
    };
    ULONGLONG Whole;
    ULONGLONG Fraction;
    UINT Index;

    if (Buffer == NULL || cchBuffer == 0)
        return;

    for (Index = 0; Index < ARRAYSIZE(Units); Index++)
    {
        if (Size >= Units[Index].Scale)
        {
            Whole = Size / Units[Index].Scale;
            Fraction = ((Size % Units[Index].Scale) * 100ULL) / Units[Index].Scale;
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%I64u.%02I64u %s",
                             Whole,
                             Fraction,
                             Units[Index].Suffix);
            return;
        }
    }

    StringCchPrintfW(Buffer,
                     cchBuffer,
                     L"%I64u B",
                     Size);
}

VOID
DmDiskViewFormatPercent(
    _In_ ULONGLONG Total,
    _In_ ULONGLONG Free,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    ULONGLONG Percent;

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Total == 0)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    Percent = (Free * 100ULL) / Total;
    StringCchPrintfW(Buffer, cchBuffer, L"%I64u%%", Percent);
}

PCWSTR
DmDiskViewPartitionStyleToString(
    _In_ PARTITION_STYLE Style)
{
    switch (Style)
    {
        case PARTITION_STYLE_MBR:
            return L"MBR";

        case PARTITION_STYLE_GPT:
            return L"GPT";

        case PARTITION_STYLE_RAW:
            return L"RAW";

        default:
            return L"Unknown";
    }
}

VOID
DmDiskViewBuildDiskSummary(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    DmDiskViewBuildDiskKind(Disk, Buffer, cchBuffer);
}

VOID
DmDiskViewBuildDiskStatus(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    DM_ACCESS_STATE AccessState;
    WCHAR AccessText[64];

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Disk == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Disk->IsOffline)
    {
        AccessState = DmAccessOffline;
    }
    else if (Disk->IsRemovable && Disk->Size == 0)
    {
        AccessState = DmAccessNoMedia;
    }
    else if (Disk->PartitionStyle == PARTITION_STYLE_RAW)
    {
        /* Match Windows wording for uninitialized disks. */
        StringCchCopyW(Buffer, cchBuffer, L"Not Initialized");
        return;
    }
    else
    {
        AccessState = DmAccessOnline;
    }

    DmGetAccessStateLabel(AccessState, AccessText, ARRAYSIZE(AccessText));
    if (Disk->IsReadOnly && AccessState == DmAccessOnline)
    {
        StringCchPrintfW(Buffer, cchBuffer, L"%s (Read Only)", AccessText);
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, AccessText);
}

VOID
DmDiskViewBuildRegionTitle(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Region->Type == DmRegionFree)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Region->DriveLetter != UNICODE_NULL)
    {
        if (Region->Label[0] != UNICODE_NULL)
        {
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%s (%C:)",
                             Region->Label,
                             towupper(Region->DriveLetter));
        }
        else
        {
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%C:",
                             towupper(Region->DriveLetter));
        }

        return;
    }

    if (Region->Label[0] != UNICODE_NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, Region->Label);
        return;
    }

    Buffer[0] = UNICODE_NULL;
}

VOID
DmDiskViewBuildRegionDetail(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR SizeText[32];

    if (Buffer == NULL || cchBuffer == 0)
        return;

    DmDiskViewFormatSize(Region->Length, SizeText, ARRAYSIZE(SizeText));

    if (Region->Type == DmRegionFree)
    {
        StringCchCopyW(Buffer, cchBuffer, SizeText);
    }
    else if (DmDiskViewIsSpecialRegion(Region))
    {
        StringCchCopyW(Buffer, cchBuffer, SizeText);
    }
    else if (Region->FileSystem[0] != UNICODE_NULL)
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"%s %s",
                         SizeText,
                         Region->FileSystem);
    }
    else
    {
        StringCchCopyW(Buffer, cchBuffer, SizeText);
    }
}

static ULONG
DmDiskViewGetVisibleRegionCount(
    _In_ const DM_DISK *Disk)
{
    ULONG Index;
    ULONG Count;

    if (Disk == NULL)
        return 0;

    Count = 0;
    for (Index = 0; Index < Disk->RegionCount; Index++)
    {
        if (!Disk->Regions[Index].IsHidden)
            Count++;
    }

    return Count;
}

static const DM_REGION *
DmDiskViewGetVisibleRegionByOrdinal(
    _In_ const DM_DISK *Disk,
    _In_ ULONG Ordinal)
{
    ULONG Index;
    ULONG VisibleIndex;

    if (Disk == NULL)
        return NULL;

    VisibleIndex = 0;
    for (Index = 0; Index < Disk->RegionCount; Index++)
    {
        if (Disk->Regions[Index].IsHidden)
            continue;

        if (VisibleIndex == Ordinal)
            return &Disk->Regions[Index];

        VisibleIndex++;
    }

    return NULL;
}

INT
DmDiskViewComputeRegionWidths(
    _In_ const DM_DISK *Disk,
    _In_ INT AvailableWidth,
    _Out_writes_opt_(RegionCount) PINT Widths,
    _In_ SIZE_T RegionCount)
{
    ULONGLONG TotalLength = 0;
    INT MinimumTotal = 0;
    INT RemainingWidth;
    INT AssignedExtra = 0;
    ULONG Index;
    ULONG VisibleCount;
    const DM_REGION *Region;

    if (Disk == NULL || Widths == NULL || RegionCount == 0)
        return 0;

    VisibleCount = DmDiskViewGetVisibleRegionCount(Disk);
    if (VisibleCount == 0)
        return 0;

    for (Index = 0; Index < VisibleCount && Index < RegionCount; Index++)
    {
        Region = DmDiskViewGetVisibleRegionByOrdinal(Disk, Index);
        if (Region == NULL)
            break;

        TotalLength += max(Region->Length, 1ULL);
        Widths[Index] = (Region->Type == DmRegionFree) ? DM_DISKVIEW_MIN_REGION_WIDTH : DM_DISKVIEW_PARTITION_WIDTH;
        MinimumTotal += Widths[Index];
    }

    if (MinimumTotal >= AvailableWidth)
    {
        INT Cursor = 0;

        for (Index = 0; Index < VisibleCount && Index < RegionCount; Index++)
        {
            Widths[Index] = max(AvailableWidth / (INT)min(VisibleCount, (ULONG)RegionCount), 1);
            Cursor += Widths[Index];
        }

        Widths[min(VisibleCount, (ULONG)RegionCount) - 1] += AvailableWidth - Cursor;
        return (INT)min(VisibleCount, (ULONG)RegionCount);
    }

    RemainingWidth = AvailableWidth - MinimumTotal;
    for (Index = 0; Index < VisibleCount && Index < RegionCount; Index++)
    {
        INT Extra;
        Region = DmDiskViewGetVisibleRegionByOrdinal(Disk, Index);
        if (Region == NULL)
            break;

        if (Index + 1 == min(VisibleCount, (ULONG)RegionCount))
        {
            Extra = RemainingWidth - AssignedExtra;
        }
        else
        {
            Extra = (INT)((Region->Length * RemainingWidth) / max(TotalLength, 1ULL));
            AssignedExtra += Extra;
        }

        Widths[Index] += Extra;
    }

    return (INT)min(VisibleCount, (ULONG)RegionCount);
}

INT
DmDiskViewGetPreferredHeight(
    _In_ const DM_SNAPSHOT *Snapshot)
{
    INT Height;

    Height = DM_DISKVIEW_MARGIN_TOP + DM_DISKVIEW_LEGEND_HEIGHT + DM_DISKVIEW_MARGIN_BOTTOM;
    if (Snapshot == NULL || Snapshot->DiskCount == 0)
        return Height;

    Height += (INT)Snapshot->DiskCount * DM_DISKVIEW_DEFAULT_ROW_HEIGHT;
    if (Snapshot->DiskCount > 1)
        Height += ((INT)Snapshot->DiskCount - 1) * DM_DISKVIEW_ROW_SPACING;

    return Height;
}

BOOL
DmDiskViewHitTest(
    _In_ const RECT *ClientRect,
    _In_ const DM_SNAPSHOT *Snapshot,
    _In_ INT ScrollOffset,
    _In_ const POINT *Point,
    _Outptr_opt_result_maybenull_ const DM_DISK **Disk,
    _Outptr_opt_result_maybenull_ const DM_REGION **Region)
{
    RECT ContentRect;
    RECT LegendRect;
    INT Y;
    ULONG DiskIndex;

    if (Disk != NULL)
        *Disk = NULL;
    if (Region != NULL)
        *Region = NULL;

    if (ClientRect == NULL || Snapshot == NULL || Point == NULL)
        return FALSE;

    ContentRect = *ClientRect;
    LegendRect = *ClientRect;
    LegendRect.top = max(ClientRect->bottom - DM_DISKVIEW_LEGEND_HEIGHT, ClientRect->top);
    ContentRect.bottom = max(LegendRect.top - 1, ContentRect.top);
    if (Point->x < ContentRect.left || Point->x >= ContentRect.right ||
        Point->y < ContentRect.top || Point->y >= ContentRect.bottom)
    {
        return FALSE;
    }

    Y = DM_DISKVIEW_MARGIN_TOP - ScrollOffset;
    for (DiskIndex = 0; DiskIndex < Snapshot->DiskCount; DiskIndex++)
    {
        RECT LabelRect;
        RECT RegionsOuterRect;
        RECT RegionsRect;
        INT AvailableWidth;
        INT x;
        ULONG VisibleCount;
        UINT RegionIndex;
        INT Widths[256];

        if (Y + DM_DISKVIEW_DEFAULT_ROW_HEIGHT < ContentRect.top)
        {
            Y += DM_DISKVIEW_DEFAULT_ROW_HEIGHT + DM_DISKVIEW_ROW_SPACING;
            continue;
        }

        if (Y >= ContentRect.bottom)
            break;

        LabelRect.left = 6;
        LabelRect.top = Y;
        LabelRect.right = min(LabelRect.left + DM_DISKVIEW_LABEL_WIDTH, ContentRect.right - 10);
        LabelRect.bottom = min(Y + DM_DISKVIEW_DEFAULT_ROW_HEIGHT, ContentRect.bottom - 6);

        if (Point->x >= LabelRect.left && Point->x < LabelRect.right &&
            Point->y >= LabelRect.top && Point->y < LabelRect.bottom)
        {
            if (Disk != NULL)
                *Disk = &Snapshot->Disks[DiskIndex];
            return TRUE;
        }

        RegionsOuterRect.left = min(LabelRect.right + 6, ContentRect.right - 6);
        RegionsOuterRect.top = Y;
        RegionsOuterRect.right = ContentRect.right - 6;
        RegionsOuterRect.bottom = LabelRect.bottom;
        RegionsRect = RegionsOuterRect;
        InflateRect(&RegionsRect, -1, -1);

        if (Point->x >= RegionsRect.left && Point->x < RegionsRect.right &&
            Point->y >= RegionsRect.top && Point->y < RegionsRect.bottom)
        {
            if (Disk != NULL)
                *Disk = &Snapshot->Disks[DiskIndex];

            AvailableWidth = RegionsRect.right - RegionsRect.left;
            VisibleCount = DmDiskViewGetVisibleRegionCount(&Snapshot->Disks[DiskIndex]);
            if (VisibleCount > 1)
                AvailableWidth -= ((INT)VisibleCount - 1) * DM_DISKVIEW_REGION_GAP;

            if (VisibleCount == 0 ||
                AvailableWidth <= 0 ||
                VisibleCount > ARRAYSIZE(Widths))
            {
                return TRUE;
            }

            DmDiskViewComputeRegionWidths(&Snapshot->Disks[DiskIndex],
                                          AvailableWidth,
                                          Widths,
                                          ARRAYSIZE(Widths));
            x = RegionsRect.left;
            for (RegionIndex = 0;
                 RegionIndex < VisibleCount && RegionIndex < ARRAYSIZE(Widths);
                 RegionIndex++)
            {
                RECT RegionRect;
                const DM_REGION *VisibleRegion;

                VisibleRegion = DmDiskViewGetVisibleRegionByOrdinal(&Snapshot->Disks[DiskIndex], RegionIndex);
                if (VisibleRegion == NULL)
                    break;

                RegionRect.left = x;
                RegionRect.top = RegionsRect.top;
                RegionRect.right = (RegionIndex + 1 == VisibleCount ||
                                    RegionIndex + 1 == ARRAYSIZE(Widths)) ?
                                   RegionsRect.right :
                                   min(x + Widths[RegionIndex], RegionsRect.right);
                RegionRect.bottom = RegionsRect.bottom;

                if (Point->x >= RegionRect.left && Point->x < RegionRect.right &&
                    Point->y >= RegionRect.top && Point->y < RegionRect.bottom)
                {
                    if (Region != NULL)
                        *Region = VisibleRegion;
                    return TRUE;
                }

                x = RegionRect.right + DM_DISKVIEW_REGION_GAP;
            }

            return TRUE;
        }

        Y += DM_DISKVIEW_DEFAULT_ROW_HEIGHT + DM_DISKVIEW_ROW_SPACING;
    }

    return FALSE;
}

static VOID
DmDiskViewPaintDisk(
    _In_ HDC hdc,
    _In_ const RECT *ClientRect,
    _In_ const DM_DISK *Disk,
    _In_opt_ const DM_VOLUME *SelectedVolume,
    _In_ INT Y,
    _In_ INT RowHeight,
    _In_opt_ HFONT Font,
    _In_opt_ HFONT BoldFont)
{
    RECT LabelRect;
    RECT RegionsOuterRect;
    RECT RegionsRect;
    INT AvailableWidth;
    INT x;
    UINT RegionIndex;
    INT Widths[256];
    BOOL DiskSelected;
    ULONG VisibleCount;

    LabelRect.left = 6;
    LabelRect.top = Y;
    LabelRect.right = min(LabelRect.left + DM_DISKVIEW_LABEL_WIDTH, ClientRect->right - 10);
    LabelRect.bottom = min(Y + RowHeight, ClientRect->bottom - 6);

    RegionsOuterRect.left = min(LabelRect.right + 6, ClientRect->right - 6);
    RegionsOuterRect.top = Y;
    RegionsOuterRect.right = ClientRect->right - 6;
    RegionsOuterRect.bottom = LabelRect.bottom;
    RegionsRect = RegionsOuterRect;
    InflateRect(&RegionsRect, -2, -2);

    DiskSelected = (SelectedVolume != NULL && DmDiskViewDiskContainsVolume(Disk, SelectedVolume));
    DmDiskViewDrawDiskLabel(hdc, &LabelRect, Disk, DiskSelected, Font, BoldFont);
    DmDiskViewDrawInsetPanel(hdc, &RegionsOuterRect, COLOR_BTNFACE);
    FillRect(hdc, &RegionsRect, GetSysColorBrush(COLOR_WINDOW));

    AvailableWidth = RegionsRect.right - RegionsRect.left;
    VisibleCount = DmDiskViewGetVisibleRegionCount(Disk);
    if (VisibleCount > 1)
        AvailableWidth -= ((INT)VisibleCount - 1) * DM_DISKVIEW_REGION_GAP;

    if (VisibleCount == 0 || AvailableWidth <= 0)
    {
        DrawTextW(hdc,
                  L"No regions",
                  -1,
                  &RegionsRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (VisibleCount > ARRAYSIZE(Widths))
    {
        AvailableWidth = 0;
    }

    if (AvailableWidth <= 0)
        return;

    DmDiskViewComputeRegionWidths(Disk, AvailableWidth, Widths, ARRAYSIZE(Widths));
    x = RegionsRect.left;

    for (RegionIndex = 0; RegionIndex < VisibleCount && RegionIndex < ARRAYSIZE(Widths); RegionIndex++)
    {
        RECT RegionRect;
        const DM_REGION *VisibleRegion;

        VisibleRegion = DmDiskViewGetVisibleRegionByOrdinal(Disk, RegionIndex);
        if (VisibleRegion == NULL)
            break;

        RegionRect.left = x;
        RegionRect.top = RegionsRect.top;
        RegionRect.right = (RegionIndex + 1 == VisibleCount || RegionIndex + 1 == ARRAYSIZE(Widths)) ?
                           RegionsRect.right :
                           min(x + Widths[RegionIndex], RegionsRect.right);
        RegionRect.bottom = RegionsRect.bottom;

        if (RegionRect.right > RegionRect.left)
        {
            DmDiskViewDrawRegion(hdc,
                                 &RegionRect,
                                 VisibleRegion,
                                 (SelectedVolume != NULL && VisibleRegion->Volume == SelectedVolume),
                                 Font,
                                 BoldFont);
        }

        x = RegionRect.right + DM_DISKVIEW_REGION_GAP;
    }
}

VOID
DmDiskViewPaintSnapshot(
    _In_ HDC hdc,
    _In_ const RECT *ClientRect,
    _In_ const DM_SNAPSHOT *Snapshot,
    _In_opt_ const DM_VOLUME *SelectedVolume,
    _In_opt_ HFONT Font,
    _In_ INT ScrollOffset)
{
    RECT rc;
    RECT ContentRect;
    RECT LegendRect;
    HGDIOBJ OldFont;
    HFONT BoldFont;
    LOGFONTW LogFont;
    INT Y;
    ULONG DiskIndex;

    if (hdc == NULL || ClientRect == NULL || Snapshot == NULL)
        return;

    rc = *ClientRect;
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
    ContentRect = rc;
    LegendRect = rc;
    LegendRect.top = max(rc.bottom - DM_DISKVIEW_LEGEND_HEIGHT, rc.top);
    ContentRect.bottom = max(LegendRect.top - 1, ContentRect.top);

    SetBkMode(hdc, TRANSPARENT);
    if (Font == NULL)
        Font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    OldFont = SelectObject(hdc, Font);
    BoldFont = Font;
    if (GetObjectW(Font, sizeof(LogFont), &LogFont) == sizeof(LogFont))
    {
        LogFont.lfWeight = FW_BOLD;
        BoldFont = CreateFontIndirectW(&LogFont);
        if (BoldFont == NULL)
            BoldFont = Font;
    }

    if (Snapshot->DiskCount == 0)
    {
        DmDiskViewPaintEmptyState(hdc, ClientRect, Font);
        SelectObject(hdc, OldFont);
        if (BoldFont != Font)
            DeleteObject(BoldFont);
        return;
    }

    Y = DM_DISKVIEW_MARGIN_TOP - ScrollOffset;
    for (DiskIndex = 0; DiskIndex < Snapshot->DiskCount; DiskIndex++)
    {
        if (Y + DM_DISKVIEW_DEFAULT_ROW_HEIGHT < ContentRect.top)
        {
            Y += DM_DISKVIEW_DEFAULT_ROW_HEIGHT + DM_DISKVIEW_ROW_SPACING;
            continue;
        }

        if (Y >= ContentRect.bottom)
            break;

        DmDiskViewPaintDisk(hdc,
                            &ContentRect,
                            &Snapshot->Disks[DiskIndex],
                            SelectedVolume,
                            Y,
                            DM_DISKVIEW_DEFAULT_ROW_HEIGHT,
                            Font,
                            BoldFont);
        Y += DM_DISKVIEW_DEFAULT_ROW_HEIGHT + DM_DISKVIEW_ROW_SPACING;
    }

    DmDiskViewPaintLegend(hdc, &LegendRect);
    if (BoldFont != Font)
        DeleteObject(BoldFont);
    SelectObject(hdc, OldFont);
}
