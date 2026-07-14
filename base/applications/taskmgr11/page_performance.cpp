/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Performance page (graphs + live statistics)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGF_CLASS L"TM11PagePerf"

enum { RES_CPU = 0, RES_MEM, RES_DISK, RES_NET, RES_COUNT };
enum { CPU_GRAPH_OVERALL = 0, CPU_GRAPH_LOGICAL = 1 };

struct PerformancePage : Page
{
    int  sel;
    int  hotTile;
    int  cpuGraphMode;
    BOOL showKernelTimes;
    BOOL trackingMouse;
    RECT tiles[RES_COUNT];
    RECT rcPane;

    PerformancePage() : sel(RES_CPU), hotTile(-1),
                        cpuGraphMode(CPU_GRAPH_LOGICAL),
                        showKernelTimes(FALSE), trackingMouse(FALSE)
    {
        ZeroMemory(tiles, sizeof(tiles));
        SetRectEmpty(&rcPane);
    }

    const WCHAR* Title() { return L"Performance"; }

    /* ---------- layout ---------- */

    void Layout()
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int railW = S(252);
        int y = rc.top + S(6);
        for (int i = 0; i < RES_COUNT; i++)
        {
            tiles[i].left = rc.left + S(6);
            tiles[i].right = rc.left + railW - S(6);
            tiles[i].top = y;
            tiles[i].bottom = y + S(78);
            y += S(84);
        }
        rcPane.left = rc.left + railW + S(10);
        rcPane.top = rc.top + S(6);
        rcPane.right = rc.right - S(16);
        rcPane.bottom = rc.bottom - S(10);
    }

    /* ---------- data helpers ---------- */

    const HistRing* Ring(int res)
    {
        switch (res)
        {
        case RES_CPU:  return &Data::g.hCpu;
        case RES_MEM:  return &Data::g.hMem;
        case RES_DISK:
            return Data::g.hDiskActive.count ? &Data::g.hDiskActive : &Data::g.hDisk;
        case RES_NET:  return &Data::g.hNetRecv;
        }
        return NULL;
    }

    static void FmtBits(double bitsPerSecond, WCHAR* buf, int cch)
    {
        if (bitsPerSecond >= 1000000000.0)
            StringCchPrintfW(buf, cch, L"%.1f Gbps", bitsPerSecond / 1000000000.0);
        else if (bitsPerSecond >= 1000000.0)
            StringCchPrintfW(buf, cch, L"%.1f Mbps", bitsPerSecond / 1000000.0);
        else
            StringCchPrintfW(buf, cch, L"%.0f Kbps", bitsPerSecond / 1000.0);
    }

    void ResourceName(int res, WCHAR* buf, int cch)
    {
        SysSnapshot& d = Data::g;
        switch (res)
        {
        case RES_CPU:
            StringCchCopyW(buf, cch, L"CPU");
            break;
        case RES_MEM:
            StringCchCopyW(buf, cch, L"Memory");
            break;
        case RES_DISK:
            if (d.diskVolumes[0])
                StringCchPrintfW(buf, cch, L"Disk %lu (%s)",
                                 d.diskNumber, d.diskVolumes);
            else
                StringCchPrintfW(buf, cch, L"Disk %lu", d.diskNumber);
            break;
        case RES_NET:
            StringCchCopyW(buf, cch, d.netType[0] ? d.netType : L"Network");
            break;
        default:
            buf[0] = 0;
            break;
        }
    }

    void TileValue(int res, WCHAR* buf, int cch)
    {
        SysSnapshot& d = Data::g;
        switch (res)
        {
        case RES_CPU:
            if (d.cpuCurMHz || d.cpuMHz)
                StringCchPrintfW(buf, cch, L"%.0f%%  %.2f GHz", d.cpuTotalPct,
                                 (d.cpuCurMHz ? d.cpuCurMHz : d.cpuMHz) / 1000.0);
            else
                StringCchPrintfW(buf, cch, L"%.0f%%", d.cpuTotalPct);
            break;
        case RES_MEM:
        {
            double used = d.memInUse / (1024.0 * 1024.0 * 1024.0);
            double tot = d.memTotal / (1024.0 * 1024.0 * 1024.0);
            StringCchPrintfW(buf, cch, L"%.1f/%.1f GB (%.0f%%)", used, tot,
                             tot > 0 ? used * 100.0 / tot : 0.0);
            break;
        }
        case RES_DISK:
        {
            if (!d.diskPresent)
                StringCchCopyW(buf, cch, L"Not available");
            else if (d.diskPerfValid)
                StringCchPrintfW(buf, cch, L"%s  %.0f%%",
                                 d.diskType, d.diskActivePct);
            else
            {
                WCHAR rate[32];
                FmtRate(d.diskReadBps + d.diskWriteBps, rate, _countof(rate));
                StringCchPrintfW(buf, cch, L"%s  %s", d.diskType, rate);
            }
            break;
        }
        case RES_NET:
        {
            WCHAR s[32], rr[32];
            if (!d.netPresent || !d.netConnected)
            {
                StringCchCopyW(buf, cch, L"Not connected");
                break;
            }
            FmtBits(d.netSendBps * 8.0, s, _countof(s));
            FmtBits(d.netRecvBps * 8.0, rr, _countof(rr));
            StringCchPrintfW(buf, cch, L"S: %s  R: %s", s, rr);
            break;
        }
        }
    }

    /* ---------- painting ---------- */

    void PaintTile(HDC dc, int res)
    {
        RECT r = tiles[res];
        BOOL isSel = (sel == res);
        BOOL isHot = (hotTile == res);

        if (isSel)
            FillRoundRect(dc, r, g_t.dark ? Blend(g_t.winBg, RGB(255,255,255), 5)
                                          : RGB(0xFF, 0xFF, 0xFF),
                          g_t.cardBorder, S(6));
        else if (isHot)
            FillRoundRect(dc, r, g_t.hoverBg, CLR_NONE, S(6));

        /* selection accent bar */
        if (isSel)
        {
            RECT bar = { r.left, r.top + S(24), r.left + S(3), r.bottom - S(24) };
            FillRoundRect(dc, bar, g_t.accent, CLR_NONE, S(2));
        }

        /* mini graph */
        RECT gr = { r.left + S(12), r.top + S(14), r.left + S(92), r.bottom - S(14) };
        GraphStyle gs;
        ZeroMemory(&gs, sizeof(gs));
        gs.line = g_t.graph[res];
        gs.border = TRUE;
        gs.yMax = (res == RES_CPU || res == RES_MEM ||
                   (res == RES_DISK && Data::g.hDiskActive.count)) ? 100.0 : 0.0;
        DrawGraph(dc, gr, Ring(res), gs);

        /* labels */
        RECT tr = { gr.right + S(12), r.top + S(12), r.right - S(6), r.top + S(32) };
        WCHAR name[96];
        ResourceName(res, name, _countof(name));
        DrawTextClip(dc, name, tr, g_t.fBodySemi, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE);
        WCHAR val[96];
        TileValue(res, val, _countof(val));
        RECT vr = { tr.left, r.top + S(34), r.right - S(4), r.bottom - S(8) };
        DrawTextClip(dc, val, vr, g_t.fSmall, g_t.textSec, DT_LEFT | DT_WORDBREAK);
    }

    struct Stat { const WCHAR* label; WCHAR value[64]; };

    static const WCHAR* FormFactorName(BYTE ff)
    {
        switch (ff)
        {
        case 0x03: return L"SIMM";
        case 0x05: return L"Chip";
        case 0x06: return L"DIP";
        case 0x09: return L"DIMM";
        case 0x0B: return L"Row of chips";
        case 0x0C: return L"RIMM";
        case 0x0D: return L"SODIMM";
        case 0x0E: return L"SRIMM";
        case 0x0F: return L"FB-DIMM";
        case 0x10: return L"Die";
        default:   return L"Unknown";
        }
    }

    int BuildStats(Stat* st, int max, WCHAR* rightTitle, int cchR, Stat* rst, int rmax)
    {
        SysSnapshot& d = Data::g;
        int n = 0, rn = 0;
        rightTitle[0] = 0;

        switch (sel)
        {
        case RES_CPU:
        {
            FmtPct(d.cpuTotalPct, st[n].value, 64);
            st[n++].label = L"Utilization";
            if (d.cpuCurMHz || d.cpuMHz)
                StringCchPrintfW(st[n].value, 64, L"%.2f GHz",
                                 (d.cpuCurMHz ? d.cpuCurMHz : d.cpuMHz) / 1000.0);
            else
                StringCchCopyW(st[n].value, 64, L"Unavailable");
            st[n++].label = L"Speed";
            FmtThousands(d.procCount, st[n].value, 64);
            st[n++].label = L"Processes";
            FmtThousands(d.threadCount, st[n].value, 64);
            st[n++].label = L"Threads";
            FmtThousands(d.handleCount, st[n].value, 64);
            st[n++].label = L"Handles";
            FmtUptime(d.upSeconds, st[n].value, 64);
            st[n++].label = L"Up time";

            if (d.cpuMHz)
                StringCchPrintfW(rst[rn].value, 64, L"%.2f GHz", d.cpuMHz / 1000.0);
            else
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            rst[rn++].label = L"Base speed:";
            StringCchPrintfW(rst[rn].value, 64, L"%d", d.sockets ? d.sockets : 1);
            rst[rn++].label = L"Sockets:";
            StringCchPrintfW(rst[rn].value, 64, L"%d", d.cores ? d.cores : d.nCpu);
            rst[rn++].label = L"Cores:";
            StringCchPrintfW(rst[rn].value, 64, L"%d", d.nCpu);
            rst[rn++].label = L"Logical processors:";
            StringCchCopyW(rst[rn].value, 64,
                           d.virtMode == 1 ? L"Enabled" : L"Disabled");
            rst[rn++].label = L"Virtualization:";
            if (d.l1KB)
                FmtBytes((ULONGLONG)d.l1KB * 1024, rst[rn].value, 64);
            else
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            rst[rn++].label = L"L1 cache:";
            if (d.l2KB)
                FmtBytes((ULONGLONG)d.l2KB * 1024, rst[rn].value, 64);
            else
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            rst[rn++].label = L"L2 cache:";
            if (d.l3KB)
                FmtBytes((ULONGLONG)d.l3KB * 1024, rst[rn].value, 64);
            else
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            rst[rn++].label = L"L3 cache:";
            break;
        }
        case RES_MEM:
        {
            FmtBytes(d.memInUse, st[n].value, 64);
            st[n++].label = L"In use";
            FmtBytes(d.memAvail, st[n].value, 64);
            st[n++].label = L"Available";
            {
                WCHAR a[32], b[32];
                FmtBytes(d.memCommit, a, _countof(a));
                FmtBytes(d.memCommitLimit, b, _countof(b));
                StringCchPrintfW(st[n].value, 64, L"%s/%s", a, b);
                st[n++].label = L"Committed";
            }
            FmtBytes(d.memCached, st[n].value, 64);
            st[n++].label = L"Cached";
            FmtBytes(d.memPagedPool, st[n].value, 64);
            st[n++].label = L"Paged pool";
            FmtBytes(d.memNonPagedPool, st[n].value, 64);
            st[n++].label = L"Non-paged pool";

            if (d.ramSpeedMTs)
            {
                StringCchPrintfW(rst[rn].value, 64, L"%u MT/s", d.ramSpeedMTs);
            }
            else
            {
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            }
            rst[rn++].label = L"Speed:";
            if (d.ramSlotsTotal)
            {
                StringCchPrintfW(rst[rn].value, 64, L"%d of %d",
                                 d.ramSlotsUsed, d.ramSlotsTotal);
            }
            else
            {
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            }
            rst[rn++].label = L"Slots used:";
            StringCchCopyW(rst[rn].value, 64, FormFactorName(d.ramFormFactor));
            rst[rn++].label = L"Form factor:";
            if (d.ramInstalled)
            {
                ULONGLONG reserved = d.ramInstalled > d.memTotal ?
                                     d.ramInstalled - d.memTotal : 0;
                FmtMemMB(reserved, rst[rn].value, 64);
            }
            else
            {
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            }
            rst[rn++].label = L"Hardware reserved:";
            break;
        }
        case RES_DISK:
        {
            if (d.diskPerfValid)
                FmtPct(d.diskActivePct, st[n].value, 64);
            else
                StringCchCopyW(st[n].value, 64, L"Unavailable");
            st[n++].label = L"Active time";
            if (d.diskPerfValid)
                StringCchPrintfW(st[n].value, 64, L"%.1f ms", d.diskResponseMs);
            else
                StringCchCopyW(st[n].value, 64, L"Unavailable");
            st[n++].label = L"Average response time";
            FmtRate(d.diskReadBps, st[n].value, 64);
            st[n++].label = L"Read speed";
            FmtRate(d.diskWriteBps, st[n].value, 64);
            st[n++].label = L"Write speed";

            FmtBytes(d.diskCapacity, rst[rn].value, 64);
            rst[rn++].label = L"Capacity:";
            FmtBytes(d.diskFormatted, rst[rn].value, 64);
            rst[rn++].label = L"Formatted:";
            StringCchCopyW(rst[rn].value, 64, d.diskSystem ? L"Yes" : L"No");
            rst[rn++].label = L"System disk:";
            StringCchCopyW(rst[rn].value, 64, d.diskPageFile ? L"Yes" : L"No");
            rst[rn++].label = L"Page file:";
            StringCchCopyW(rst[rn].value, 64, d.diskType);
            rst[rn++].label = L"Type:";
            StringCchCopyW(rst[rn].value, 64, d.diskInterface);
            rst[rn++].label = L"Interface:";
            StringCchCopyW(rightTitle, cchR, d.diskModel);
            break;
        }
        case RES_NET:
        {
            FmtBits(d.netSendBps * 8.0, st[n].value, 64);
            st[n++].label = L"Send";
            FmtBits(d.netRecvBps * 8.0, st[n].value, 64);
            st[n++].label = L"Receive";

            StringCchCopyW(rst[rn].value, 64,
                           d.netAdapter[0] ? d.netAdapter : L"Unavailable");
            rst[rn++].label = L"Adapter name:";
            StringCchCopyW(rst[rn].value, 64,
                           d.netDns[0] ? d.netDns : L"Unavailable");
            rst[rn++].label = L"DNS name:";
            StringCchCopyW(rst[rn].value, 64,
                           d.netType[0] ? d.netType : L"Unavailable");
            rst[rn++].label = L"Connection type:";
            StringCchCopyW(rst[rn].value, 64,
                           d.netIpv4[0] ? d.netIpv4 : L"Unavailable");
            rst[rn++].label = L"IPv4 address:";
            StringCchCopyW(rst[rn].value, 64,
                           d.netIpv6[0] ? d.netIpv6 : L"Unavailable");
            rst[rn++].label = L"IPv6 address:";
            if (d.netLinkBps)
            {
                FmtBits((double)d.netLinkBps, rst[rn].value, 64);
            }
            else
            {
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            }
            rst[rn++].label = L"Link speed:";
            StringCchCopyW(rightTitle, cchR, d.netName);
            break;
        }
        }
        (void)max; (void)rmax;
        return n | (rn << 8);
    }

    static double AutoMaximum(const HistRing* first,
                              const HistRing* second,
                              double floorValue)
    {
        double peak = first ? first->Max() : 0;
        if (second && second->Max() > peak)
            peak = second->Max();
        double maximum = floorValue;
        while (maximum < peak)
            maximum *= 2.0;
        return maximum;
    }

    void PaintHistoryGraph(HDC dc,
                           const RECT& graph,
                           const WCHAR* axis,
                           const WCHAR* maximumLabel,
                           const HistRing* history,
                           const HistRing* second,
                           double maximum,
                           BOOL unavailable,
                           COLORREF secondLine = 0)
    {
        RECT axisRect = { graph.left, graph.top - S(18),
                          graph.right, graph.top - S(2) };
        DrawTextClip(dc, axis, axisRect, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE);
        DrawTextClip(dc, maximumLabel, axisRect, g_t.fSmall, g_t.textSec,
                     DT_RIGHT | DT_SINGLELINE);

        GraphStyle style;
        ZeroMemory(&style, sizeof(style));
        style.line = g_t.graph[sel];
        style.secondLine = secondLine;
        style.grid = TRUE;
        style.border = TRUE;
        style.yMax = maximum;
        style.second = second;
        DrawGraph(dc, graph, history, style);

        if (unavailable)
        {
            RECT textRect = graph;
            DrawTextClip(dc, L"Performance counters unavailable", textRect,
                         g_t.fBody, g_t.textSec,
                         DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        RECT bottomRect = { graph.left, graph.bottom + S(2),
                            graph.right, graph.bottom + S(18) };
        DrawTextClip(dc, L"60 seconds", bottomRect, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE);
        DrawTextClip(dc, L"0", bottomRect, g_t.fSmall, g_t.textSec,
                     DT_RIGHT | DT_SINGLELINE);
    }

    COLORREF KernelLineColor(void)
    {
        return g_t.dark ? RGB(0xFF, 0x73, 0x73) : RGB(0xC5, 0x2B, 0x32);
    }

    void PaintLogicalCpuGraphs(HDC dc, const RECT& graph)
    {
        SysSnapshot& d = Data::g;
        int processorCount = d.nCpu;
        if (processorCount < 1) processorCount = 1;
        if (processorCount > 64) processorCount = 64;

        int columns;
        if (processorCount <= 2) columns = processorCount;
        else if (processorCount <= 4) columns = 2;
        else if (processorCount <= 16) columns = 4;
        else columns = 8;
        int rows = (processorCount + columns - 1) / columns;
        int gap = S(4);
        int cellWidth = (graph.right - graph.left - gap * (columns - 1)) / columns;
        int cellHeight = (graph.bottom - graph.top - gap * (rows - 1)) / rows;

        RECT axisRect = { graph.left, graph.top - S(18),
                          graph.right, graph.top - S(2) };
        DrawTextClip(dc, L"% Utilization", axisRect, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE);
        DrawTextClip(dc, L"100%", axisRect, g_t.fSmall, g_t.textSec,
                     DT_RIGHT | DT_SINGLELINE);

        for (int i = 0; i < processorCount; i++)
        {
            int column = i % columns;
            int row = i / columns;
            RECT cell = {
                graph.left + column * (cellWidth + gap),
                graph.top + row * (cellHeight + gap),
                graph.left + column * (cellWidth + gap) + cellWidth,
                graph.top + row * (cellHeight + gap) + cellHeight
            };
            GraphStyle style;
            ZeroMemory(&style, sizeof(style));
            style.line = g_t.graph[RES_CPU];
            style.secondLine = KernelLineColor();
            style.grid = TRUE;
            style.border = TRUE;
            style.yMax = 100.0;
            style.second = showKernelTimes ? &d.hCpuLogicalKernel[i] : NULL;
            DrawGraph(dc, cell, &d.hCpuLogical[i], style);
        }

        RECT bottomRect = { graph.left, graph.bottom + S(2),
                            graph.right, graph.bottom + S(18) };
        DrawTextClip(dc, L"60 seconds", bottomRect, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE);
        DrawTextClip(dc, L"0", bottomRect, g_t.fSmall, g_t.textSec,
                     DT_RIGHT | DT_SINGLELINE);
    }

    void PaintMetric(HDC dc,
                     int x,
                     int y,
                     int width,
                     const Stat& stat,
                     HFONT valueFont)
    {
        RECT label = { x, y, x + width - S(6), y + S(15) };
        DrawTextClip(dc, stat.label, label, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE);
        RECT value = { x, label.bottom, x + width - S(6), label.bottom + S(31) };
        DrawTextClip(dc, stat.value, value, valueFont, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE | DT_TOP);
    }

    void PaintPair(HDC dc,
                   int x,
                   int y,
                   int width,
                   int labelWidth,
                   const Stat& stat)
    {
        RECT label = { x, y, x + labelWidth, y + S(19) };
        DrawTextClip(dc, stat.label, label, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT value = { label.right, label.top, x + width, label.bottom };
        DrawTextClip(dc, stat.value, value, g_t.fSmall, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    int GetStats(Stat* stats, Stat* rightStats)
    {
        WCHAR title[160];
        return BuildStats(stats, 12, title, _countof(title), rightStats, 12);
    }

    void PaintCpuStats(HDC dc, const RECT& pane, int y)
    {
        Stat stats[12], rightStats[12];
        int packed = GetStats(stats, rightStats);
        int rightCount = packed >> 8;
        int width = pane.right - pane.left;
        int rightX = pane.left + width * 59 / 100;
        int leftWidth = rightX - pane.left - S(18);

        int half = leftWidth / 2;
        PaintMetric(dc, pane.left, y, half, stats[0], g_t.fBig);
        PaintMetric(dc, pane.left + half, y, leftWidth - half, stats[1], g_t.fBig);

        int third = leftWidth / 3;
        int row2 = y + S(49);
        PaintMetric(dc, pane.left, row2, third, stats[2], g_t.fMed);
        PaintMetric(dc, pane.left + third, row2, third, stats[3], g_t.fMed);
        PaintMetric(dc, pane.left + third * 2, row2,
                    leftWidth - third * 2, stats[4], g_t.fMed);
        PaintMetric(dc, pane.left, y + S(94), leftWidth, stats[5], g_t.fMed);

        int rightWidth = pane.right - rightX;
        int labelWidth = rightWidth * 57 / 100;
        for (int i = 0; i < rightCount; i++)
            PaintPair(dc, rightX, y + i * S(19), rightWidth,
                      labelWidth, rightStats[i]);
    }

    void PaintMemoryComposition(HDC dc, const RECT& pane, int y, int* statsY)
    {
        SysSnapshot& d = Data::g;
        RECT title = { pane.left, y, pane.right, y + S(16) };
        DrawTextClip(dc, L"Memory composition", title,
                     g_t.fSmall, g_t.textSec, DT_LEFT | DT_SINGLELINE);

        RECT composition = { pane.left, title.bottom + S(3),
                             pane.right, title.bottom + S(29) };
        if (d.memTotal)
        {
            ULONGLONG cachedAvailable = d.memCached < d.memAvail ?
                                        d.memCached : d.memAvail;
            int totalWidth = composition.right - composition.left;
            int inUseWidth = (int)((double)totalWidth * d.memInUse / d.memTotal);
            int cachedWidth = (int)((double)totalWidth * cachedAvailable / d.memTotal);
            if (inUseWidth < 0) inUseWidth = 0;
            if (inUseWidth > totalWidth) inUseWidth = totalWidth;
            if (cachedWidth < 0) cachedWidth = 0;
            if (inUseWidth + cachedWidth > totalWidth)
                cachedWidth = totalWidth - inUseWidth;

            RECT inUse = { composition.left, composition.top,
                           composition.left + inUseWidth, composition.bottom };
            RECT cached = { inUse.right, composition.top,
                            inUse.right + cachedWidth, composition.bottom };
            RECT freePart = { cached.right, composition.top,
                              composition.right, composition.bottom };
            FillRect32(dc, inUse, g_t.graph[RES_MEM]);
            FillRect32(dc, cached, Blend(g_t.graph[RES_MEM], g_t.listBg, 62));
            FillRect32(dc, freePart, g_t.listBg);
            if (inUse.right > composition.left && inUse.right < composition.right)
                DrawVLine(dc, inUse.right, composition.top, composition.bottom,
                          g_t.graph[RES_MEM]);
            if (cached.right > composition.left && cached.right < composition.right)
                DrawVLine(dc, cached.right, composition.top, composition.bottom,
                          g_t.graph[RES_MEM]);
        }
        FillRoundRect(dc, composition, CLR_NONE, g_t.graph[RES_MEM], 0);
        *statsY = composition.bottom + S(14);
    }

    void PaintMemoryStats(HDC dc, const RECT& pane, int y)
    {
        Stat stats[12], rightStats[12];
        int packed = GetStats(stats, rightStats);
        int count = packed & 0xFF;
        int rightCount = packed >> 8;
        int width = pane.right - pane.left;
        int rightX = pane.left + width * 62 / 100;
        int leftWidth = rightX - pane.left - S(18);
        int columnWidth = leftWidth / 2;
        for (int i = 0; i < count; i++)
        {
            int column = i % 2;
            int row = i / 2;
            PaintMetric(dc, pane.left + column * columnWidth,
                        y + row * S(43),
                        column == 0 ? columnWidth : leftWidth - columnWidth,
                        stats[i], g_t.fMed);
        }

        int rightWidth = pane.right - rightX;
        int labelWidth = rightWidth * 58 / 100;
        for (int i = 0; i < rightCount; i++)
            PaintPair(dc, rightX, y + i * S(21), rightWidth,
                      labelWidth, rightStats[i]);
    }

    void PaintDiskStats(HDC dc, const RECT& pane, int y)
    {
        Stat stats[12], rightStats[12];
        int packed = GetStats(stats, rightStats);
        int count = packed & 0xFF;
        int rightCount = packed >> 8;
        int width = pane.right - pane.left;
        int rightX = pane.left + width * 61 / 100;
        int leftWidth = rightX - pane.left - S(18);
        int columnWidth = leftWidth / 2;
        for (int i = 0; i < count; i++)
        {
            int column = i % 2;
            int row = i / 2;
            PaintMetric(dc, pane.left + column * columnWidth,
                        y + row * S(45),
                        column == 0 ? columnWidth : leftWidth - columnWidth,
                        stats[i], g_t.fMed);
        }

        int rightWidth = pane.right - rightX;
        int labelWidth = rightWidth * 56 / 100;
        for (int i = 0; i < rightCount; i++)
            PaintPair(dc, rightX, y + i * S(19), rightWidth,
                      labelWidth, rightStats[i]);
    }

    void PaintNetworkStats(HDC dc, const RECT& pane, int y)
    {
        Stat stats[12], rightStats[12];
        int packed = GetStats(stats, rightStats);
        int rightCount = packed >> 8;
        int width = pane.right - pane.left;
        int rightX = pane.left + width * 50 / 100;
        int leftWidth = rightX - pane.left - S(18);
        int half = leftWidth / 2;
        PaintMetric(dc, pane.left, y, half, stats[0], g_t.fBig);
        PaintMetric(dc, pane.left + half, y, leftWidth - half, stats[1], g_t.fBig);

        int rightWidth = pane.right - rightX;
        int labelWidth = rightWidth * 39 / 100;
        for (int i = 0; i < rightCount; i++)
            PaintPair(dc, rightX, y + i * S(20), rightWidth,
                      labelWidth, rightStats[i]);
    }

    void PaintPane(HDC dc)
    {
        SysSnapshot& d = Data::g;
        RECT pane = rcPane;

        WCHAR resource[96];
        ResourceName(sel, resource, _countof(resource));
        RECT header = { pane.left, pane.top, pane.right, pane.top + S(34) };
        DrawTextClip(dc, resource, header, g_t.fTitle, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE | DT_TOP);

        const WCHAR* hardware = L"";
        WCHAR hardwareBuffer[160];
        switch (sel)
        {
        case RES_CPU:
            hardware = d.cpuName;
            break;
        case RES_MEM:
            FmtBytes(d.ramInstalled ? d.ramInstalled : d.memTotal,
                     hardwareBuffer, _countof(hardwareBuffer));
            hardware = hardwareBuffer;
            break;
        case RES_DISK:
            hardware = d.diskPresent ? d.diskModel : L"Not available";
            break;
        case RES_NET:
            hardware = d.netPresent ? d.netAdapter : L"Not available";
            break;
        }
        DrawTextClip(dc, hardware, header, g_t.fBody, g_t.textSec,
                     DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);

        int graphTop = pane.top + S(52);
        if (sel == RES_CPU)
        {
            int graphBottom = pane.bottom - S(170);
            if (graphBottom < graphTop + S(90))
                graphBottom = graphTop + S(90);
            RECT graph = { pane.left, graphTop, pane.right, graphBottom };
            if (cpuGraphMode == CPU_GRAPH_LOGICAL && d.nCpu > 1)
                PaintLogicalCpuGraphs(dc, graph);
            else
                PaintHistoryGraph(dc, graph, L"% Utilization", L"100%",
                                  &d.hCpu,
                                  showKernelTimes ? &d.hCpuKernel : NULL,
                                  100.0, FALSE,
                                  showKernelTimes ? KernelLineColor() : 0);
            PaintCpuStats(dc, pane, graph.bottom + S(24));
        }
        else if (sel == RES_MEM)
        {
            int graphBottom = pane.bottom - S(225);
            if (graphBottom < graphTop + S(80))
                graphBottom = graphTop + S(80);
            RECT graph = { pane.left, graphTop, pane.right, graphBottom };
            WCHAR maximum[64];
            FmtBytes(d.memTotal, maximum, _countof(maximum));
            PaintHistoryGraph(dc, graph, L"Memory usage", maximum,
                              &d.hMem, NULL, 100.0, FALSE);
            int statsY;
            PaintMemoryComposition(dc, pane, graph.bottom + S(23), &statsY);
            PaintMemoryStats(dc, pane, statsY);
        }
        else if (sel == RES_DISK)
        {
            int statsY = pane.bottom - S(125);
            int graphLimit = statsY - S(24);
            int available = graphLimit - graphTop;
            int gap = S(40);
            if (available < S(180))
                available = S(180);
            int firstHeight = (available - gap) * 58 / 100;
            int secondHeight = available - gap - firstHeight;
            RECT activeGraph = { pane.left, graphTop, pane.right,
                                 graphTop + firstHeight };
            RECT transferGraph = { pane.left, activeGraph.bottom + gap,
                                   pane.right, activeGraph.bottom + gap + secondHeight };
            PaintHistoryGraph(dc, activeGraph, L"Active time", L"100%",
                              &d.hDiskActive, NULL, 100.0,
                              !d.diskPerfValid && !d.hDiskActive.count);

            double maximum = AutoMaximum(&d.hDisk, NULL, 100.0 * 1024.0);
            WCHAR maximumLabel[64];
            FmtRate(maximum, maximumLabel, _countof(maximumLabel));
            PaintHistoryGraph(dc, transferGraph, L"Disk transfer rate",
                              maximumLabel, &d.hDisk, NULL, maximum, FALSE);
            PaintDiskStats(dc, pane, transferGraph.bottom + S(24));
        }
        else
        {
            int graphBottom = pane.bottom - S(150);
            if (graphBottom < graphTop + S(90))
                graphBottom = graphTop + S(90);
            RECT graph = { pane.left, graphTop, pane.right, graphBottom };
            double maximum = AutoMaximum(&d.hNetRecv, &d.hNetSend, 12500.0);
            WCHAR maximumLabel[64];
            FmtBits(maximum * 8.0, maximumLabel, _countof(maximumLabel));
            PaintHistoryGraph(dc, graph, L"Throughput", maximumLabel,
                              &d.hNetRecv, &d.hNetSend, maximum, FALSE);
            PaintNetworkStats(dc, pane, graph.bottom + S(24));
        }
    }

    void Paint(HDC dc, const RECT& rcPaint)
    {
        FillRect32(dc, rcPaint, g_t.listBg);
        Layout();
        for (int i = 0; i < RES_COUNT; i++)
            PaintTile(dc, i);
        DrawVLine(dc, tiles[0].right + S(4), rcPane.top, rcPane.bottom, g_t.divider);
        PaintPane(dc);
    }

    /* ---------- clipboard ---------- */

    void CopyStats()
    {
        WCHAR buf[1024] = L"";
        WCHAR resource[96];
        ResourceName(sel, resource, _countof(resource));
        StringCchPrintfW(buf, _countof(buf), L"%s\r\n", resource);
        Stat st[12], rst[12];
        WCHAR rightTitle[160];
        int packed = BuildStats(st, 12, rightTitle, _countof(rightTitle), rst, 12);
        int n = packed & 0xFF, rn = packed >> 8;
        for (int i = 0; i < n; i++)
        {
            WCHAR line[128];
            StringCchPrintfW(line, _countof(line), L"\t%s\t%s\r\n",
                             st[i].label, st[i].value);
            StringCchCatW(buf, _countof(buf), line);
        }
        for (int i = 0; i < rn; i++)
        {
            WCHAR line[128];
            StringCchPrintfW(line, _countof(line), L"\t%s\t%s\r\n",
                             rst[i].label, rst[i].value);
            StringCchCatW(buf, _countof(buf), line);
        }

        if (OpenClipboard(hwnd))
        {
            EmptyClipboard();
            SIZE_T cb = (lstrlenW(buf) + 1) * sizeof(WCHAR);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cb);
            if (h)
            {
                void* p = GlobalLock(h);
                if (p)
                {
                    CopyMemory(p, buf, cb);
                    GlobalUnlock(h);
                    SetClipboardData(CF_UNICODETEXT, h);
                }
            }
            CloseClipboard();
        }
    }

    void ShowContextMenu(POINT screenPoint)
    {
        if (sel != RES_CPU)
        {
            MItem items[] =
            {
                { 1, L"Copy", 0, NULL, 0 },
            };
            if (Menu_Show(hwnd, screenPoint, items, _countof(items)) == 1)
                CopyStats();
            return;
        }

        MItem graphModes[] =
        {
            { 10, L"Overall utilization",
              cpuGraphMode == CPU_GRAPH_OVERALL ? MIF_RADIO : 0u, NULL, 0 },
            { 11, L"Logical processors",
              cpuGraphMode == CPU_GRAPH_LOGICAL ? MIF_RADIO : 0u, NULL, 0 },
        };
        MItem items[] =
        {
            { 0, L"Change graph to", 0, graphModes, _countof(graphModes) },
            { 12, L"Show kernel times", showKernelTimes ? MIF_CHECKED : 0u, NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 1, L"Copy", 0, NULL, 0 },
        };
        UINT command = Menu_Show(hwnd, screenPoint, items, _countof(items));
        if (command == 1)
            CopyStats();
        else if (command == 10 || command == 11)
        {
            cpuGraphMode = command == 11 ? CPU_GRAPH_LOGICAL : CPU_GRAPH_OVERALL;
            InvalidateRect(hwnd, &rcPane, FALSE);
        }
        else if (command == 12)
        {
            showKernelTimes = !showKernelTimes;
            InvalidateRect(hwnd, &rcPane, FALSE);
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_COPY, L"Copy", IC_COPY, BS_SUBTLE);
    }

    void OnCommand(int id)
    {
        if (id == CMD_COPY) CopyStats();
    }

    void OnTick()
    {
        if (IsWindowVisible(hwnd))
            InvalidateRect(hwnd, NULL, FALSE);
    }

    void OnThemeChanged()
    {
        InvalidateRect(hwnd, NULL, FALSE);
    }

    HWND Create(HWND parent);
};

static PerformancePage* s_page;

static LRESULT CALLBACK PgPerfProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PerformancePage* pg = s_page;
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BufPaint bp;
        HDC dc = bp.Begin(hdc, &ps.rcPaint);
        pg->Paint(dc, ps.rcPaint);
        bp.End();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE:
    {
        if (!pg->trackingMouse)
        {
            TRACKMOUSEEVENT tracking;
            ZeroMemory(&tracking, sizeof(tracking));
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            if (TrackMouseEvent(&tracking))
                pg->trackingMouse = TRUE;
        }
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hot = -1;
        for (int i = 0; i < RES_COUNT; i++)
            if (PtInRect(&pg->tiles[i], pt)) { hot = i; break; }
        if (hot != pg->hotTile)
        {
            pg->hotTile = hot;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        pg->trackingMouse = FALSE;
        pg->hotTile = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        for (int i = 0; i < RES_COUNT; i++)
        {
            if (PtInRect(&pg->tiles[i], pt))
            {
                pg->sel = i;
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
        }
        SetFocus(hwnd);
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        pg->ShowContextMenu(pt);
        return 0;
    }
    case WM_CONTEXTMENU:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x == -1 && pt.y == -1)
        {
            pt.x = pg->rcPane.left + S(40);
            pt.y = pg->rcPane.top + S(80);
            ClientToScreen(hwnd, &pt);
        }
        pg->ShowContextMenu(pt);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_UP && pg->sel > 0)
        {
            pg->sel--;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (wp == VK_DOWN && pg->sel < RES_COUNT - 1)
        {
            pg->sel++;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            pg->CopyStats();
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND PerformancePage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgPerfProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGF_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGF_CLASS, L"", WS_CHILD,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    return hwnd;
}

Page* CreatePerformancePage(void)
{
    s_page = new PerformancePage();
    return s_page;
}
