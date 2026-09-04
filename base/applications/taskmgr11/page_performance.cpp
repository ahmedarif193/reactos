/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Performance page (graphs + live statistics)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGF_CLASS L"TM11PagePerf"

enum { RES_CPU = 0, RES_MEM, RES_DISK, RES_NET, RES_GPU, RES_COUNT };
enum { CPU_GRAPH_OVERALL = 0, CPU_GRAPH_LOGICAL = 1 };

/* -1 shows every engine at once, which is what Windows opens with; any
 * other value names the single engine to fill the graph area with. */
#define GPU_GRAPH_ALL_ENGINES (-1)

#define MAX_PERF_TILES (TM_MAX_DISKS + TM_MAX_NICS + TM_MAX_GPUS + 2)

struct PerformancePage : Page
{
    int  sel;
    int  hotTile;
    int  railScroll;
    int  cpuGraphMode;
    int  gpuGraphMode;
    BOOL showKernelTimes;
    BOOL trackingMouse;
    BOOL selectionRestored;
    RECT tiles[MAX_PERF_TILES];
    RECT railClip;
    RECT rcPane;

    PerformancePage() : sel(0), hotTile(-1), railScroll(0),
                        cpuGraphMode(CPU_GRAPH_LOGICAL),
                        gpuGraphMode(GPU_GRAPH_ALL_ENGINES),
                        showKernelTimes(FALSE), trackingMouse(FALSE),
                        selectionRestored(FALSE)
    {
        ZeroMemory(tiles, sizeof(tiles));
        SetRectEmpty(&railClip);
        SetRectEmpty(&rcPane);
    }

    const WCHAR* Title() { return L"Performance"; }

    /* CPU, memory, one per disk, one per network adapter, one per display
     * adapter -- the same order Windows lists them in. */
    int TileCount(void) const
    {
        return Data::g.diskCount + Data::g.netCount + Data::g.gpuCount + 2;
    }

    int TileResource(int tile) const
    {
        if (tile == 0) return RES_CPU;
        if (tile == 1) return RES_MEM;
        if (tile < 2 + Data::g.diskCount) return RES_DISK;
        if (tile < 2 + Data::g.diskCount + Data::g.netCount) return RES_NET;
        return RES_GPU;
    }

    DiskSnapshot* TileDisk(int tile)
    {
        int index = tile - 2;
        if (index < 0 || index >= Data::g.diskCount)
            return NULL;
        return &Data::g.disks[index];
    }

    NetSnapshot* TileNet(int tile)
    {
        int index = tile - (2 + Data::g.diskCount);
        if (index < 0 || index >= Data::g.netCount)
            return NULL;
        return &Data::g.nets[index];
    }

    GpuSnapshot* TileGpu(int tile)
    {
        int index = tile - (2 + Data::g.diskCount + Data::g.netCount);
        if (index < 0 || index >= Data::g.gpuCount)
            return NULL;
        return &Data::g.gpus[index];
    }

    int SelectedResource(void) const
    {
        return TileResource(sel);
    }

    DiskSnapshot* SelectedDisk(void)
    {
        return TileDisk(sel);
    }

    NetSnapshot* SelectedNet(void)
    {
        return TileNet(sel);
    }

    GpuSnapshot* SelectedGpu(void)
    {
        return TileGpu(sel);
    }

    /* Which one of its kind a tile is: disk 2, GPU 1, and so on.  CPU,
     * memory and network have exactly one each, so their index is zero. */
    int TileIndex(int tile) const
    {
        int resource = TileResource(tile);

        if (resource == RES_DISK)
            return tile - 2;
        if (resource == RES_NET)
            return tile - (2 + Data::g.diskCount);
        if (resource == RES_GPU)
            return tile - (2 + Data::g.diskCount + Data::g.netCount);
        return 0;
    }

    /*
     * Reopening Task Manager comes back to the resource it was last showing.
     * The selection is remembered as a kind plus an index rather than a tile
     * number, because a disk that appeared or a GPU that went away would
     * otherwise shift the same number onto a different resource.
     */
    void RememberSelection(void)
    {
        g_app.st.perfResource = (DWORD)SelectedResource();
        g_app.st.perfIndex = (DWORD)TileIndex(sel);
    }

    void RestoreSelection(void)
    {
        int count = TileCount();

        if (selectionRestored)
            return;
        /* Nothing is restorable until the first sample has told us which
         * disks and adapters exist. */
        if (count <= 2)
            return;
        selectionRestored = TRUE;
        for (int i = 0; i < count; i++)
        {
            if ((DWORD)TileResource(i) == g_app.st.perfResource &&
                (DWORD)TileIndex(i) == g_app.st.perfIndex)
            {
                sel = i;
                EnsureSelectedVisible();
                return;
            }
        }
    }

    /* The engines drawn for a GPU, capped at what fits the graph area. */
    int GpuVisibleEngines(const GpuSnapshot* gpu) const
    {
        int count = gpu ? gpu->engineCount : 0;
        return count > 4 ? 4 : count;
    }

    /* ---------- layout ---------- */

    void Layout()
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int railW = S(252);
        railClip.left = rc.left;
        railClip.top = rc.top + S(6);
        railClip.right = rc.left + railW;
        railClip.bottom = rc.bottom - S(10);

        int count = TileCount();
        int totalHeight = count * S(84) - S(6);
        int viewportHeight = railClip.bottom - railClip.top;
        int maxScroll = totalHeight > viewportHeight ?
                        totalHeight - viewportHeight : 0;
        if (railScroll < 0) railScroll = 0;
        if (railScroll > maxScroll) railScroll = maxScroll;

        int y = railClip.top - railScroll;
        for (int i = 0; i < count; i++)
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

    void EnsureSelectedVisible(void)
    {
        Layout();
        if (sel < 0 || sel >= TileCount())
            return;
        if (tiles[sel].top < railClip.top)
            railScroll -= railClip.top - tiles[sel].top;
        else if (tiles[sel].bottom > railClip.bottom)
            railScroll += tiles[sel].bottom - railClip.bottom;
        Layout();
    }

    void ScrollRail(int amount)
    {
        int oldScroll = railScroll;
        railScroll += amount;
        Layout();
        if (railScroll != oldScroll)
        {
            hotTile = -1;
            InvalidateRect(hwnd, &railClip, FALSE);
        }
    }

    /* ---------- data helpers ---------- */

    const HistRing* Ring(int tile)
    {
        int res = TileResource(tile);
        switch (res)
        {
        case RES_CPU:  return &Data::g.hCpu;
        case RES_MEM:  return &Data::g.hMem;
        case RES_DISK:
        {
            DiskSnapshot* disk = TileDisk(tile);
            if (!disk) return NULL;
            return disk->hActive.count ? &disk->hActive : &disk->hTransfer;
        }
        case RES_NET:
        {
            NetSnapshot* net = TileNet(tile);
            return net ? &net->hRecv : NULL;
        }
        case RES_GPU:
        {
            GpuSnapshot* gpu = TileGpu(tile);
            return gpu ? &gpu->hUtil : NULL;
        }
        }
        return NULL;
    }

    /* "1.6/12.0 GB" -- one unit for the pair, which is how Windows shows a
     * used-of-total figure so the two halves stay comparable at a glance. */
    static void FmtMemoryPair(ULONGLONG used, ULONGLONG total, WCHAR* buf, int cch)
    {
        const double gigabyte = 1024.0 * 1024.0 * 1024.0;
        const double megabyte = 1024.0 * 1024.0;

        /* One decimal either way, and the same unit the graph beside it is
         * scaled in, so the pair and the graph's ceiling read as the same
         * number rather than one rounded copy of the other. */
        if (total >= (ULONGLONG)gigabyte)
        {
            StringCchPrintfW(buf, cch, L"%.1f/%.1f GB",
                             used / gigabyte, total / gigabyte);
            return;
        }
        StringCchPrintfW(buf, cch, L"%.1f/%.1f MB",
                         used / megabyte, total / megabyte);
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

    void ResourceName(int tile, WCHAR* buf, int cch)
    {
        int res = TileResource(tile);
        switch (res)
        {
        case RES_CPU:
            StringCchCopyW(buf, cch, L"CPU");
            break;
        case RES_MEM:
            StringCchCopyW(buf, cch, L"Memory");
            break;
        case RES_DISK:
        {
            DiskSnapshot* disk = TileDisk(tile);
            if (!disk)
            {
                StringCchCopyW(buf, cch, L"Disk");
                break;
            }
            if (disk->volumes[0])
                StringCchPrintfW(buf, cch, L"Disk %lu (%s)",
                                 disk->number, disk->volumes);
            else
                StringCchPrintfW(buf, cch, L"Disk %lu", disk->number);
            break;
        }
        case RES_NET:
        {
            NetSnapshot* net = TileNet(tile);
            StringCchCopyW(buf, cch,
                           (net && net->type[0]) ? net->type : L"Network");
            break;
        }
        case RES_GPU:
        {
            GpuSnapshot* gpu = TileGpu(tile);
            StringCchPrintfW(buf, cch, L"GPU %d", gpu ? gpu->index : 0);
            break;
        }
        default:
            buf[0] = 0;
            break;
        }
    }

    void TileValue(int tile, WCHAR* buf, int cch)
    {
        SysSnapshot& d = Data::g;
        int res = TileResource(tile);
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
            DiskSnapshot* disk = TileDisk(tile);
            if (!disk || !disk->present)
                StringCchCopyW(buf, cch, L"Not available");
            else if (disk->perfValid)
                FmtPct(disk->activePct, buf, cch);
            else
                FmtRate(disk->readBps + disk->writeBps, buf, cch);
            break;
        }
        case RES_NET:
        {
            NetSnapshot* net = TileNet(tile);
            WCHAR sent[32], received[32];
            if (!net || !net->connected)
            {
                StringCchCopyW(buf, cch, L"Not connected");
                break;
            }
            FmtBits(net->sendBps * 8.0, sent, _countof(sent));
            FmtBits(net->recvBps * 8.0, received, _countof(received));
            StringCchPrintfW(buf, cch, L"S: %s  R: %s", sent, received);
            break;
        }
        case RES_GPU:
        {
            GpuSnapshot* gpu = TileGpu(tile);
            if (!gpu)
            {
                StringCchCopyW(buf, cch, L"Not available");
                break;
            }
            if (gpu->hasTemperature)
                StringCchPrintfW(buf, cch, L"%.0f%% (%.0f \u00B0C)",
                                 gpu->utilPct, gpu->temperatureC);
            else
                StringCchPrintfW(buf, cch, L"%.0f%%", gpu->utilPct);
            break;
        }
        }
    }

    /*
     * The adapter's own name, shown above its utilization, is what makes two
     * identically-named GPU tiles tell each other apart.  It is a separate
     * line rather than part of the value because a long adapter name has to
     * be truncated with an ellipsis, and a wrapped one would push the
     * utilization out of the tile.
     */
    BOOL TileSubtitle(int tile, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        switch (TileResource(tile))
        {
        case RES_GPU:
        {
            GpuSnapshot* gpu = TileGpu(tile);
            if (!gpu)
                return FALSE;
            StringCchCopyW(buf, cch, gpu->name);
            return TRUE;
        }
        case RES_DISK:
        {
            DiskSnapshot* disk = TileDisk(tile);
            if (!disk || !disk->present || !disk->type[0])
                return FALSE;
            StringCchCopyW(buf, cch, disk->type);
            return TRUE;
        }
        case RES_NET:
        {
            NetSnapshot* net = TileNet(tile);
            if (!net || !net->name[0])
                return FALSE;
            StringCchCopyW(buf, cch, net->name);
            return TRUE;
        }
        default:
            return FALSE;
        }
    }

    /* ---------- painting ---------- */

    void PaintTile(HDC dc, int tile)
    {
        int res = TileResource(tile);
        RECT r = tiles[tile];
        BOOL isSel = (sel == tile);
        BOOL isHot = (hotTile == tile);

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
        /* A percentage series is drawn against a fixed 100% ceiling; a rate
         * series has no natural ceiling and auto-scales. */
        gs.yMax = (res == RES_CPU || res == RES_MEM || res == RES_GPU ||
                   (res == RES_DISK && TileDisk(tile) &&
                    TileDisk(tile)->hActive.count)) ? 100.0 : 0.0;
        DrawGraph(dc, gr, Ring(tile), gs);

        /* labels */
        RECT tr = { gr.right + S(12), r.top + S(12), r.right - S(6), r.top + S(32) };
        WCHAR name[160];
        ResourceName(tile, name, _countof(name));
        DrawTextClip(dc, name, tr, g_t.fBodySemi, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE);
        WCHAR val[96];
        WCHAR subtitle[160];
        TileValue(tile, val, _countof(val));
        if (TileSubtitle(tile, subtitle, _countof(subtitle)))
        {
            RECT sr = { tr.left, r.top + S(32), r.right - S(4), r.top + S(48) };
            RECT vr = { tr.left, r.top + S(48), r.right - S(4), r.bottom - S(8) };
            DrawTextClip(dc, subtitle, sr, g_t.fSmall, g_t.textSec,
                         DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawTextClip(dc, val, vr, g_t.fSmall, g_t.textSec,
                         DT_LEFT | DT_SINGLELINE);
            return;
        }
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

        switch (SelectedResource())
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
            DiskSnapshot* disk = SelectedDisk();
            if (!disk)
                break;
            if (disk->perfValid)
                FmtPct(disk->activePct, st[n].value, 64);
            else
                StringCchCopyW(st[n].value, 64, L"Unavailable");
            st[n++].label = L"Active time";
            if (disk->perfValid)
                StringCchPrintfW(st[n].value, 64, L"%.1f ms", disk->responseMs);
            else
                StringCchCopyW(st[n].value, 64, L"Unavailable");
            st[n++].label = L"Average response time";
            FmtRate(disk->readBps, st[n].value, 64);
            st[n++].label = L"Read speed";
            FmtRate(disk->writeBps, st[n].value, 64);
            st[n++].label = L"Write speed";

            FmtBytes(disk->capacity, rst[rn].value, 64);
            rst[rn++].label = L"Capacity:";
            FmtBytes(disk->formatted, rst[rn].value, 64);
            rst[rn++].label = L"Formatted:";
            StringCchCopyW(rst[rn].value, 64, disk->system ? L"Yes" : L"No");
            rst[rn++].label = L"System disk:";
            StringCchCopyW(rst[rn].value, 64, disk->pageFile ? L"Yes" : L"No");
            rst[rn++].label = L"Page file:";
            StringCchCopyW(rst[rn].value, 64, disk->type);
            rst[rn++].label = L"Type:";
            StringCchCopyW(rst[rn].value, 64, disk->interfaceName);
            rst[rn++].label = L"Interface:";
            StringCchCopyW(rightTitle, cchR, disk->model);
            break;
        }
        case RES_NET:
        {
            NetSnapshot* net = SelectedNet();
            if (!net)
                break;

            FmtBits(net->sendBps * 8.0, st[n].value, 64);
            st[n++].label = L"Send";
            FmtBits(net->recvBps * 8.0, st[n].value, 64);
            st[n++].label = L"Receive";

            StringCchCopyW(rst[rn].value, 64,
                           net->adapter[0] ? net->adapter : L"Unavailable");
            rst[rn++].label = L"Adapter name:";
            StringCchCopyW(rst[rn].value, 64,
                           net->dns[0] ? net->dns : L"Unavailable");
            rst[rn++].label = L"DNS name:";
            StringCchCopyW(rst[rn].value, 64,
                           net->type[0] ? net->type : L"Unavailable");
            rst[rn++].label = L"Connection type:";
            StringCchCopyW(rst[rn].value, 64,
                           net->ipv4[0] ? net->ipv4 : L"Unavailable");
            rst[rn++].label = L"IPv4 address:";
            StringCchCopyW(rst[rn].value, 64,
                           net->ipv6[0] ? net->ipv6 : L"Unavailable");
            rst[rn++].label = L"IPv6 address:";
            if (net->linkBps)
            {
                FmtBits((double)net->linkBps, rst[rn].value, 64);
            }
            else
            {
                StringCchCopyW(rst[rn].value, 64, L"Unavailable");
            }
            rst[rn++].label = L"Link speed:";
            StringCchCopyW(rightTitle, cchR, net->name);
            break;
        }
        case RES_GPU:
        {
            GpuSnapshot* gpu = SelectedGpu();
            if (!gpu)
                break;

            FmtPct(gpu->utilPct, st[n].value, 64);
            st[n++].label = L"Utilization";
            FmtMemoryPair(gpu->dedicatedUsed, gpu->dedicatedTotal, st[n].value, 64);
            st[n++].label = L"Dedicated GPU memory";
            FmtMemoryPair(gpu->dedicatedUsed + gpu->sharedUsed,
                          gpu->dedicatedTotal + gpu->sharedTotal, st[n].value, 64);
            st[n++].label = L"GPU Memory";
            FmtMemoryPair(gpu->sharedUsed, gpu->sharedTotal, st[n].value, 64);
            st[n++].label = L"Shared GPU memory";
            /*
             * A driver that reports no thermals leaves the row out entirely,
             * exactly as Windows does; a zero here would read as a real
             * reading of zero degrees.
             */
            if (gpu->hasTemperature)
            {
                StringCchPrintfW(st[n].value, 64, L"%.0f \u00B0C", gpu->temperatureC);
                st[n++].label = L"GPU Temperature";
            }

            StringCchCopyW(rst[rn].value, 64,
                           gpu->driverVersion[0] ? gpu->driverVersion : L"Unavailable");
            rst[rn++].label = L"Driver version:";
            StringCchCopyW(rst[rn].value, 64,
                           gpu->driverDate[0] ? gpu->driverDate : L"Unavailable");
            rst[rn++].label = L"Driver date:";
            StringCchCopyW(rst[rn].value, 64,
                           gpu->directX[0] ? gpu->directX : L"Unavailable");
            rst[rn++].label = L"DirectX version:";
            StringCchCopyW(rst[rn].value, 64,
                           gpu->location[0] ? gpu->location : L"Unavailable");
            rst[rn++].label = L"Physical location:";
            FmtMemMB(gpu->reserved, rst[rn].value, 64);
            rst[rn++].label = L"Hardware reserved memory:";
            StringCchCopyW(rightTitle, cchR, gpu->name);
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
        style.line = g_t.graph[SelectedResource()];
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

        {
            GraphPaint paint(dc);
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
                DrawGraph(paint, cell, &d.hCpuLogical[i], style);
            }
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

    /*
     * One graph per engine, laid out the way Windows lays them out: a single
     * engine fills the row, two share it, and more than two fall into a grid.
     * Each cell carries its own name and full-scale label, because an engine
     * graph with no name is unreadable next to three others.
     */
    void PaintGpuEngines(HDC dc, const RECT& area, GpuSnapshot* gpu)
    {
        int count = GpuVisibleEngines(gpu);
        int columns, rows;
        int gap = S(10);
        int labelHeight = S(16);
        int cellWidth, cellHeight;

        if (count <= 0)
        {
            DrawTextClip(dc, L"No GPU engines reported", area, g_t.fBody,
                         g_t.textSec, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            return;
        }

        columns = (count == 1) ? 1 : 2;
        rows = (count + columns - 1) / columns;
        cellWidth = (area.right - area.left - gap * (columns - 1)) / columns;
        cellHeight = (area.bottom - area.top - gap * (rows - 1)) / rows;
        if (cellHeight <= labelHeight + S(12))
            cellHeight = labelHeight + S(12);

        {
            GraphPaint paint(dc);
            for (int i = 0; i < count; i++)
            {
                int column = i % columns;
                int row = i / columns;
                int left = area.left + column * (cellWidth + gap);
                int top = area.top + row * (cellHeight + gap);
                RECT cell = { left, top + labelHeight, left + cellWidth, top + cellHeight };
                GraphStyle style;

                ZeroMemory(&style, sizeof(style));
                style.line = g_t.graph[GR_GPU];
                style.grid = TRUE;
                style.border = TRUE;
                style.yMax = 100.0;
                DrawGraph(paint, cell, &gpu->engines[i].history, style);
            }
        }

        for (int i = 0; i < count; i++)
        {
            int column = i % columns;
            int row = i / columns;
            int left = area.left + column * (cellWidth + gap);
            int top = area.top + row * (cellHeight + gap);
            RECT label = { left, top, left + cellWidth, top + labelHeight };
            WCHAR percent[16];

            DrawTextClip(dc, gpu->engines[i].name, label, g_t.fSmall, g_t.textSec,
                         DT_LEFT | DT_SINGLELINE | DT_BOTTOM);
            StringCchPrintfW(percent, _countof(percent), L"%.0f%%",
                             gpu->engines[i].utilPct);
            DrawTextClip(dc, percent, label, g_t.fSmall, g_t.textSec,
                         DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);
        }
    }

    void PaintGpuStats(HDC dc, const RECT& pane, int y)
    {
        Stat stats[12], rightStats[12];
        int packed = GetStats(stats, rightStats);
        int count = packed & 0xFF;
        int rightCount = packed >> 8;
        int width = pane.right - pane.left;
        int rightX = pane.left + width * 52 / 100;
        int leftWidth = rightX - pane.left - S(18);
        /*
         * The second column carries the long labels -- "Dedicated GPU memory"
         * and "Shared GPU memory" -- while the first carries short ones, so
         * the split is uneven on purpose: an even one clips the very labels
         * that say which memory the number beside them is.
         */
        int columnWidth = leftWidth * 42 / 100;

        for (int i = 0; i < count; i++)
        {
            int column = i % 2;
            int row = i / 2;
            PaintMetric(dc, pane.left + column * columnWidth,
                        y + row * S(43),
                        column == 0 ? columnWidth : leftWidth - columnWidth,
                        stats[i], g_t.fMed);
        }

        {
            int rightWidth = pane.right - rightX;
            /* "Hardware reserved memory:" is the widest label on this pane and
             * the split is sized so it never has to be clipped. */
            int labelWidth = rightWidth * 60 / 100;
            for (int i = 0; i < rightCount; i++)
                PaintPair(dc, rightX, y + i * S(19), rightWidth,
                          labelWidth, rightStats[i]);
        }
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

        int resourceType = SelectedResource();
        DiskSnapshot* disk = SelectedDisk();
        WCHAR resource[160];
        ResourceName(sel, resource, _countof(resource));
        RECT header = { pane.left, pane.top, pane.right, pane.top + S(34) };
        DrawTextClip(dc, resource, header, g_t.fTitle, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE | DT_TOP);

        const WCHAR* hardware = L"";
        WCHAR hardwareBuffer[160];
        switch (resourceType)
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
            hardware = disk && disk->present ? disk->model : L"Not available";
            break;
        case RES_NET:
        {
            NetSnapshot* net = SelectedNet();
            hardware = net ? net->adapter : L"Not available";
            break;
        }
        case RES_GPU:
        {
            GpuSnapshot* gpu = SelectedGpu();
            hardware = gpu ? gpu->name : L"Not available";
            break;
        }
        }
        DrawTextClip(dc, hardware, header, g_t.fBody, g_t.textSec,
                     DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);

        int graphTop = pane.top + S(52);
        if (resourceType == RES_CPU)
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
        else if (resourceType == RES_MEM)
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
        else if (resourceType == RES_DISK && disk)
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
                              &disk->hActive, NULL, 100.0,
                              !disk->perfValid && !disk->hActive.count);

            double maximum = AutoMaximum(&disk->hTransfer, NULL,
                                         100.0 * 1024.0);
            WCHAR maximumLabel[64];
            FmtRate(maximum, maximumLabel, _countof(maximumLabel));
            PaintHistoryGraph(dc, transferGraph, L"Disk transfer rate",
                              maximumLabel, &disk->hTransfer, NULL,
                              maximum, FALSE);
            PaintDiskStats(dc, pane, transferGraph.bottom + S(24));
        }
        else if (resourceType == RES_GPU)
        {
            GpuSnapshot* gpu = SelectedGpu();
            int statsY = pane.bottom - S(112);
            int graphLimit = statsY - S(24);
            int available = graphLimit - graphTop;
            int gap = S(30);
            int enginesHeight;
            int dedicatedHeight;
            int sharedHeight;
            WCHAR maximumLabel[64];

            if (!gpu)
                return;
            if (available < S(240))
                available = S(240);

            /*
             * The engine block takes the top half and the two memory graphs
             * split the rest, which keeps a four-engine grid readable while
             * still giving each memory graph a usable height.
             */
            enginesHeight = (available - gap * 2) * 50 / 100;
            /* A display-only adapter reports no engines at all.  Leaving an
             * empty half-pane where the engine graphs would be says nothing;
             * the memory graphs take the room instead. */
            if (gpu->engineCount == 0)
                enginesHeight = 0;
            dedicatedHeight = (available - gap * 2 - enginesHeight) / 2;
            sharedHeight = available - gap * 2 - enginesHeight - dedicatedHeight;

            if (enginesHeight != 0)
            {
                RECT engines = { pane.left, graphTop, pane.right,
                                 graphTop + enginesHeight };
                if (gpuGraphMode == GPU_GRAPH_ALL_ENGINES ||
                    gpuGraphMode >= gpu->engineCount)
                {
                    PaintGpuEngines(dc, engines, gpu);
                }
                else
                {
                    /* A single named engine gets the whole block, which is the
                     * only way to read a spiky engine at this scale. */
                    WCHAR percent[16];
                    StringCchPrintfW(percent, _countof(percent), L"100%%");
                    PaintHistoryGraph(dc, engines,
                                      gpu->engines[gpuGraphMode].name, percent,
                                      &gpu->engines[gpuGraphMode].history,
                                      NULL, 100.0, FALSE);
                }
            }

            {
                int memoryTop = (enginesHeight != 0)
                                    ? graphTop + enginesHeight + gap
                                    : graphTop;
                RECT dedicated = { pane.left, memoryTop, pane.right,
                                   memoryTop + dedicatedHeight };
                RECT shared = { pane.left, dedicated.bottom + gap, pane.right,
                                dedicated.bottom + gap + sharedHeight };
                double dedicatedMax = gpu->dedicatedTotal ?
                                      (double)gpu->dedicatedTotal :
                                      AutoMaximum(&gpu->hDedicated, NULL,
                                                  64.0 * 1024.0 * 1024.0);
                double sharedMax = gpu->sharedTotal ?
                                   (double)gpu->sharedTotal :
                                   AutoMaximum(&gpu->hShared, NULL,
                                               64.0 * 1024.0 * 1024.0);

                FmtBytes((ULONGLONG)dedicatedMax, maximumLabel, _countof(maximumLabel));
                PaintHistoryGraph(dc, dedicated, L"Dedicated GPU memory usage",
                                  maximumLabel, &gpu->hDedicated, NULL,
                                  dedicatedMax, FALSE);
                FmtBytes((ULONGLONG)sharedMax, maximumLabel, _countof(maximumLabel));
                PaintHistoryGraph(dc, shared, L"Shared GPU memory usage",
                                  maximumLabel, &gpu->hShared, NULL,
                                  sharedMax, FALSE);
                PaintGpuStats(dc, pane, shared.bottom + S(24));
            }
        }
        else
        {
            NetSnapshot* net = SelectedNet();
            int graphBottom = pane.bottom - S(150);
            if (graphBottom < graphTop + S(90))
                graphBottom = graphTop + S(90);
            RECT graph = { pane.left, graphTop, pane.right, graphBottom };
            double maximum;
            WCHAR maximumLabel[64];

            if (!net)
                return;
            maximum = AutoMaximum(&net->hRecv, &net->hSend, 12500.0);
            FmtBits(maximum * 8.0, maximumLabel, _countof(maximumLabel));
            PaintHistoryGraph(dc, graph, L"Throughput", maximumLabel,
                              &net->hRecv, &net->hSend, maximum, FALSE);
            PaintNetworkStats(dc, pane, graph.bottom + S(24));
        }
    }

    void PaintRailScrollThumb(HDC dc)
    {
        int viewportHeight = railClip.bottom - railClip.top;
        int totalHeight = TileCount() * S(84) - S(6);
        if (viewportHeight <= 0 || totalHeight <= viewportHeight)
            return;

        int thumbHeight = viewportHeight * viewportHeight / totalHeight;
        if (thumbHeight < S(24)) thumbHeight = S(24);
        int maxScroll = totalHeight - viewportHeight;
        int travel = viewportHeight - thumbHeight;
        int top = railClip.top + (maxScroll ? railScroll * travel / maxScroll : 0);
        RECT thumb = { railClip.right - S(5), top,
                       railClip.right - S(2), top + thumbHeight };
        FillRoundRect(dc, thumb, g_t.scrollThumb, CLR_NONE, S(2));
    }

    void Paint(HDC dc, const RECT& rcPaint)
    {
        FillRect32(dc, rcPaint, g_t.listBg);
        RestoreSelection();
        Layout();
        int saved = SaveDC(dc);
        IntersectClipRect(dc, railClip.left, railClip.top,
                         railClip.right, railClip.bottom);
        for (int i = 0; i < TileCount(); i++)
        {
            RECT visible;
            if (IntersectRect(&visible, &tiles[i], &railClip))
                PaintTile(dc, i);
        }
        RestoreDC(dc, saved);
        PaintRailScrollThumb(dc);
        DrawVLine(dc, tiles[0].right + S(4), rcPane.top, rcPane.bottom, g_t.divider);
        PaintPane(dc);
    }

    /* ---------- clipboard ---------- */

    void CopyStats()
    {
        WCHAR buf[2048] = L"";
        WCHAR resource[160];
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
        if (SelectedResource() == RES_GPU)
        {
            GpuSnapshot* gpu = SelectedGpu();
            MItem graphModes[TM_MAX_GPU_ENGINES + 1];
            MItem items[3];
            int modeCount = 0;
            int engines = gpu ? gpu->engineCount : 0;
            UINT command;

            graphModes[modeCount].id = 20;
            graphModes[modeCount].text = L"Multiple engines";
            graphModes[modeCount].flags =
                (gpuGraphMode == GPU_GRAPH_ALL_ENGINES) ? MIF_RADIO : 0u;
            graphModes[modeCount].sub = NULL;
            graphModes[modeCount].nSub = 0;
            modeCount++;
            for (int i = 0; i < engines && modeCount < (int)_countof(graphModes); i++)
            {
                graphModes[modeCount].id = 21 + i;
                graphModes[modeCount].text = gpu->engines[i].name;
                graphModes[modeCount].flags = (gpuGraphMode == i) ? MIF_RADIO : 0u;
                graphModes[modeCount].sub = NULL;
                graphModes[modeCount].nSub = 0;
                modeCount++;
            }

            items[0].id = 0;
            items[0].text = L"Change graph to";
            items[0].flags = 0;
            items[0].sub = graphModes;
            items[0].nSub = modeCount;
            items[1].id = 0;
            items[1].text = NULL;
            items[1].flags = MIF_SEP;
            items[1].sub = NULL;
            items[1].nSub = 0;
            items[2].id = 1;
            items[2].text = L"Copy";
            items[2].flags = 0;
            items[2].sub = NULL;
            items[2].nSub = 0;

            command = Menu_Show(hwnd, screenPoint, items, _countof(items));
            if (command == 1)
            {
                CopyStats();
            }
            else if (command == 20)
            {
                gpuGraphMode = GPU_GRAPH_ALL_ENGINES;
                InvalidateRect(hwnd, &rcPane, FALSE);
            }
            else if (command >= 21 && command < 21u + (UINT)engines)
            {
                gpuGraphMode = (int)(command - 21);
                InvalidateRect(hwnd, &rcPane, FALSE);
            }
            return;
        }

        if (SelectedResource() != RES_CPU)
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
    if (pg && !pg->hwnd)
        pg->hwnd = hwnd;
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
        if (pg->SelectedResource() == RES_CPU)
            App_SmpDiagGraphPaint();
        return 0;
    }
    case WM_SIZE:
        pg->EnsureSelectedVisible();
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
        for (int i = 0; i < pg->TileCount(); i++)
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
        for (int i = 0; i < pg->TileCount(); i++)
        {
            if (PtInRect(&pg->tiles[i], pt))
            {
                pg->sel = i;
                pg->RememberSelection();
                pg->EnsureSelectedVisible();
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
        }
        SetFocus(hwnd);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (pt.x >= pg->railClip.left && pt.x < pg->railClip.right)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            pg->ScrollRail(-delta * S(84) / WHEEL_DELTA);
        }
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
            pg->RememberSelection();
            pg->EnsureSelectedVisible();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (wp == VK_DOWN && pg->sel < pg->TileCount() - 1)
        {
            pg->sel++;
            pg->RememberSelection();
            pg->EnsureSelectedVisible();
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
