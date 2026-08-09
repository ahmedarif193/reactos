/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Vector icon set, drawn with GDI+ at any size (resolution
 *              independent). All outlines are from Lucide 1.16.0, ISC/MIT
 *              licensed - see res/LICENSE-lucide.txt. The SVG path data is
 *              mechanically normalized to absolute M/L/C/Z commands (arcs
 *              converted to cubic Beziers) so the parser stays trivial.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#include <stdlib.h>

using namespace Gdiplus;

/* Lucide path data (24x24 unit space), see source map in the license file */
static const char*
IconPathData(IconId icon)
{
    switch (icon)
    {
    case IC_NONE:
        return NULL;

    case IC_HAMBURGER: /* menu.svg */
        return
        "M4 5 L20 5 M4 12 L20 12 M4 19 L20 19";

    case IC_PROCESSES: /* gauge.svg */
        return
        "M12 14 L16 10 M3.34 19 C0.913 14.797 1.854 9.459 5.572 6.339 C9.29 3.22 14.71 3.22 "
        "18.428 6.339 C22.146 9.459 23.087 14.797 20.66 19";

    case IC_PERF: /* activity.svg */
        return
        "M22 12 L19.52 12 C18.622 11.998 17.833 12.595 17.59 13.46 L15.24 21.82 C15.209 "
        "21.927 15.111 22 15 22 C14.889 22 14.791 21.927 14.76 21.82 L9.24 2.18 C9.209 2.073 "
        "9.111 2 9 2 C8.889 2 8.791 2.073 8.76 2.18 L6.41 10.54 C6.168 11.401 5.384 11.997 "
        "4.49 12 L2 12";

    case IC_HISTORY: /* history.svg */
        return
        "M3 12 C3 16.971 7.029 21 12 21 C16.971 21 21 16.971 21 12 C21 7.029 16.971 3 12 3 "
        "C9.484 3.009 7.069 3.991 5.26 5.74 L3 8 M3 3 L3 8 L8 8 M12 7 L12 12 L16 14";

    case IC_STARTUP: /* rocket.svg */
        return
        "M12 15 L12 20 C12 20 15.03 19.45 16 18 C17.08 16.38 16 13 16 13 M4.5 16.5 C3 17.76 "
        "2.5 21.5 2.5 21.5 C2.5 21.5 6.24 21 7.5 19.5 C8.21 18.66 8.2 17.37 7.41 16.59 C6.605 "
        "15.822 5.351 15.783 4.5 16.5 M9 12 C9.532 10.619 10.202 9.296 11 8.05 C13.369 4.262 "
        "17.532 1.972 22 2 C22 4.72 21.22 9.5 16 13 C14.737 13.798 13.397 14.468 12 15 Z M9 "
        "12 L4 12 C4 12 4.55 8.97 6 8 C7.62 6.92 11 8.05 11 8.05";

    case IC_USERS: /* users.svg */
        return
        "M16 21 L16 19 C16 16.791 14.209 15 12 15 L6 15 C3.791 15 2 16.791 2 19 L2 21 M16 "
        "3.128 C17.764 3.585 18.996 5.177 18.996 7 C18.996 8.823 17.764 10.415 16 10.872 M22 "
        "21 L22 19 C21.999 17.177 20.765 15.586 19 15.13 M5 7 C5 9.209 6.791 11 9 11 C11.209 "
        "11 13 9.209 13 7 C13 4.791 11.209 3 9 3 C6.791 3 5 4.791 5 7";

    case IC_DETAILS: /* list.svg */
        return
        "M3 5 L3.01 5 M3 12 L3.01 12 M3 19 L3.01 19 M8 5 L21 5 M8 12 L21 12 M8 19 L21 19";

    case IC_SERVICES: /* wrench.svg */
        return
        "M14.7 6.3 C14.319 6.689 14.319 7.311 14.7 7.7 L16.3 9.3 C16.689 9.681 17.311 9.681 "
        "17.7 9.3 L20.806 6.195 C21.126 5.873 21.669 5.975 21.789 6.413 C22.406 8.656 21.67 "
        "11.054 19.901 12.565 C18.133 14.077 15.65 14.429 13.53 13.47 L5.62 21.38 C4.792 "
        "22.208 3.449 22.208 2.621 21.379 C1.792 20.551 1.793 19.208 2.621 18.38 L10.531 "
        "10.47 C9.572 8.35 9.924 5.867 11.436 4.099 C12.947 2.33 15.345 1.594 17.588 2.211 "
        "C18.026 2.331 18.128 2.873 17.807 3.195 Z";

    case IC_SENSORS: /* thermometer.svg */
        return
        "M14 4 C14 2.895 13.105 2 12 2 C10.895 2 10 2.895 10 4 L10 13.76 C8.19 14.58 7.188 "
        "16.538 7.594 18.483 C8 20.427 9.716 21.82 11.701 21.992 C13.686 22.164 15.615 21.08 "
        "16.348 19.227 C17.08 17.374 16.478 15.26 14.86 14.096 C14.592 13.904 14.304 13.741 14 13.61 Z "
        "M12 6 L12 17 M10 17 C10 18.105 10.895 19 12 19 C13.105 19 14 18.105 14 17 C14 15.895 "
        "13.105 15 12 15 C10.895 15 10 15.895 10 17";

    case IC_SETTINGS: /* settings.svg */
        return
        "M9.671 4.136 C9.785 2.935 10.794 2.017 12 2.017 C13.207 2.017 14.216 2.935 14.33 "
        "4.136 C14.397 4.896 14.831 5.575 15.491 5.957 C16.152 6.338 16.957 6.373 17.649 "
        "6.051 C18.745 5.553 20.04 5.969 20.643 7.011 C21.245 8.054 20.958 9.383 19.979 "
        "10.084 C19.355 10.522 18.983 11.237 18.983 11.999 C18.983 12.762 19.355 13.477 "
        "19.979 13.915 C20.958 14.616 21.245 15.945 20.643 16.988 C20.04 18.03 18.745 18.446 "
        "17.649 17.948 C16.957 17.626 16.152 17.661 15.491 18.042 C14.831 18.424 14.397 "
        "19.103 14.33 19.863 C14.216 21.064 13.207 21.982 12.001 21.982 C10.794 21.982 9.785 "
        "21.064 9.671 19.863 C9.604 19.103 9.17 18.423 8.509 18.042 C7.848 17.66 7.043 17.625 "
        "6.351 17.948 C5.255 18.446 3.96 18.03 3.357 16.988 C2.755 15.945 3.042 14.616 4.021 "
        "13.915 C4.645 13.477 5.017 12.762 5.017 11.999 C5.017 11.237 4.645 10.522 4.021 "
        "10.084 C3.044 9.383 2.757 8.054 3.359 7.013 C3.961 5.971 5.254 5.555 6.35 6.051 "
        "C7.042 6.373 7.847 6.338 8.508 5.957 C9.168 5.575 9.602 4.896 9.669 4.136 M9 12 C9 "
        "13.657 10.343 15 12 15 C13.657 15 15 13.657 15 12 C15 10.343 13.657 9 12 9 C10.343 9 "
        "9 10.343 9 12";

    case IC_SEARCH: /* search.svg */
        return
        "M21 21 L16.66 16.66 M3 11 C3 15.418 6.582 19 11 19 C15.418 19 19 15.418 19 11 C19 "
        "6.582 15.418 3 11 3 C6.582 3 3 6.582 3 11";

    case IC_RUNTASK: /* square-terminal.svg */
        return
        "M7 11 L9 9 L7 7 M11 13 L15 13 M5 3 L19 3 C20.105 3 21 3.895 21 5 L21 19 C21 "
        "20.105 20.105 21 19 21 L5 21 C3.895 21 3 20.105 3 19 L3 5 C3 3.895 3.895 3 5 3 Z";

    case IC_LEAF: /* leaf.svg */
        return
        "M11 20 C7.359 20.011 4.318 17.229 4.005 13.602 C3.692 9.975 6.211 6.713 9.8 6.1 "
        "C15.5 5 17 4.48 19 2 C20 4 21 6.18 21 10 C21 15.5 16.22 20 11 20 Z M2 21 C2 18 3.85 "
        "15.64 7.08 15 C9.5 14.52 12 13 13 12";

    case IC_PAUSE: /* pause.svg */
        return
        "M15 3 L18 3 C18.552 3 19 3.448 19 4 L19 20 C19 20.552 18.552 21 18 21 L15 21 C14.448 "
        "21 14 20.552 14 20 L14 4 C14 3.448 14.448 3 15 3 Z M6 3 L9 3 C9.552 3 10 3.448 10 4 "
        "L10 20 C10 20.552 9.552 21 9 21 L6 21 C5.448 21 5 20.552 5 20 L5 4 C5 3.448 5.448 3 "
        "6 3 Z";

    case IC_CHEV_R: /* chevron-right.svg */
        return
        "M9 18 L15 12 L9 6";

    case IC_CHEV_D: /* chevron-down.svg */
        return
        "M6 9 L12 15 L18 9";

    case IC_CHEV_U: /* chevron-up.svg */
        return
        "M18 15 L12 9 L6 15";

    case IC_MORE: /* ellipsis.svg */
        return
        "M11 12 C11 12.552 11.448 13 12 13 C12.552 13 13 12.552 13 12 C13 11.448 12.552 11 12 "
        "11 C11.448 11 11 11.448 11 12 M18 12 C18 12.552 18.448 13 19 13 C19.552 13 20 12.552 "
        "20 12 C20 11.448 19.552 11 19 11 C18.448 11 18 11.448 18 12 M4 12 C4 12.552 4.448 13 "
        "5 13 C5.552 13 6 12.552 6 12 C6 11.448 5.552 11 5 11 C4.448 11 4 11.448 4 12";

    case IC_MIN: /* minus.svg */
        return
        "M5 12 L19 12";

    case IC_MAX: /* square.svg */
        return
        "M5 3 L19 3 C20.105 3 21 3.895 21 5 L21 19 C21 20.105 20.105 21 19 21 L5 21 C3.895 "
        "21 3 20.105 3 19 L3 5 C3 3.895 3.895 3 5 3 Z";

    case IC_RESTORE: /* copy.svg */
    case IC_COPY: /* copy.svg */
        return
        "M10 8 L20 8 C21.105 8 22 8.895 22 10 L22 20 C22 21.105 21.105 22 20 22 L10 22 C8.895 "
        "22 8 21.105 8 20 L8 10 C8 8.895 8.895 8 10 8 Z M4 16 C2.9 16 2 15.1 2 14 L2 4 C2 2.9 "
        "2.9 2 4 2 L14 2 C15.1 2 16 2.9 16 4";

    case IC_DISCONNECT: /* monitor-x.svg */
        return
        "M14.5 12.5 L9.5 7.5 M9.5 12.5 L14.5 7.5 M4 3 L20 3 C21.105 3 22 3.895 22 5 L22 15 "
        "C22 16.105 21.105 17 20 17 L4 17 C2.895 17 2 16.105 2 15 L2 5 C2 3.895 2.895 3 4 3 Z "
        "M12 17 L12 21 M8 21 L16 21";

    case IC_SIGNOUT: /* log-out.svg */
        return
        "M16 17 L21 12 L16 7 M21 12 L9 12 M9 21 L5 21 C3.895 21 3 20.105 3 19 L3 5 C3 3.895 "
        "3.895 3 5 3 L9 3";

    case IC_DISABLE: /* ban.svg */
        return
        "M2 12 C2 17.523 6.477 22 12 22 C17.523 22 22 17.523 22 12 C22 6.477 17.523 2 12 2 "
        "C6.477 2 2 6.477 2 12 M4.929 4.929 L19.07 19.071";

    case IC_REFRESH: /* refresh-cw.svg */
        return
        "M3 12 C3 7.029 7.029 3 12 3 C14.516 3.009 16.931 3.991 18.74 5.74 L21 8 M21 3 L21 8 "
        "L16 8 M21 12 C21 16.971 16.971 21 12 21 C9.484 20.991 7.069 20.009 5.26 18.26 L3 16 "
        "M8 16 L3 16 L3 21";

    case IC_OPENFOLDER: /* folder-open.svg */
        return
        "M6 14 L7.5 11.1 C7.832 10.44 8.501 10.017 9.24 10 L20 10 C20.619 9.999 21.204 10.285 "
        "21.584 10.774 C21.963 11.263 22.095 11.9 21.94 12.5 L20.4 18.5 C20.171 19.388 19.367 "
        "20.006 18.45 20 L4 20 C2.895 20 2 19.105 2 18 L2 5 C2 3.895 2.895 3 4 3 L7.9 3 C8.58 "
        "2.993 9.216 3.332 9.59 3.9 L10.4 5.1 C10.77 5.662 11.397 6 12.07 6 L18 6 C19.105 6 "
        "20 6.895 20 8 L20 10";

    case IC_OPENAPP: /* external-link.svg */
        return
        "M15 3 L21 3 L21 9 M10 14 L21 3 M18 13 L18 19 C18 20.105 17.105 21 16 21 L5 21 "
        "C3.895 21 3 20.105 3 19 L3 8 C3 6.895 3.895 6 5 6 L11 6";

    case IC_WINDOW: /* app-window.svg */
        return
        "M4 4 L20 4 C21.105 4 22 4.895 22 6 L22 18 C22 19.105 21.105 20 20 20 L4 20 C2.895 20 "
        "2 19.105 2 18 L2 6 C2 4.895 2.895 4 4 4 Z M10 4 L10 8 M2 8 L22 8 M6 4 L6 8";

    case IC_ENDTASK: /* x.svg */
    case IC_CLOSE: /* x.svg */
        return
        "M18 6 L6 18 M6 6 L18 18";

    case IC_CHECK: /* check.svg */
    case IC_ENABLE: /* check.svg */
        return
        "M20 6 L9 17 L4 12";

    case IC_COUNT:
        return NULL;
    }

    return NULL;
}

/* Minimal parser for the normalized path strings above: absolute
   M x y / L x y / C x1 y1 x2 y2 x y / Z only, blank separated. */
static void
BuildIconPath(GraphicsPath& path, const char* d)
{
    float cx = 0.0f, cy = 0.0f;   /* current point */
    float fx = 0.0f, fy = 0.0f;   /* figure start (for Z) */
    const char* p = d;

    while (*p)
    {
        char cmd = *p;
        if (cmd == ' ')
        {
            ++p;
            continue;
        }
        ++p;

        float v[6];
        int need = (cmd == 'C') ? 6 : (cmd == 'Z') ? 0 : 2;
        for (int i = 0; i < need; ++i)
        {
            char* end;
            v[i] = (float)strtod(p, &end);
            p = end;
        }

        switch (cmd)
        {
        case 'M':
            path.StartFigure();
            cx = fx = v[0];
            cy = fy = v[1];
            break;
        case 'L':
            path.AddLine(PointF(cx, cy), PointF(v[0], v[1]));
            cx = v[0];
            cy = v[1];
            break;
        case 'C':
            path.AddBezier(PointF(cx, cy), PointF(v[0], v[1]),
                           PointF(v[2], v[3]), PointF(v[4], v[5]));
            cx = v[4];
            cy = v[5];
            break;
        case 'Z':
            path.CloseFigure();
            cx = fx;
            cy = fy;
            break;
        default:
            return;   /* malformed - draw what we have */
        }
    }
}

void DrawGlyph(HDC dc, const RECT& r, IconId icon, COLORREF c)
{
    const char* d = IconPathData(icon);
    if (!d) return;

    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    int w = r.right - r.left, h = r.bottom - r.top;
    float s = (float)(w < h ? w : h);
    float x = r.left + (w - s) / 2.0f;
    float y = r.top + (h - s) / 2.0f;

    /* Lucide stroke weight: 2/24 of the box, kept readable at tiny sizes */
    float lw = s / 12.0f;
    if (lw < 1.2f) lw = 1.2f;
    Pen pen(GP(c), lw);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    GraphicsPath path(FillModeWinding);
    BuildIconPath(path, d);

    /* 24-unit Lucide space -> pixel box */
    Matrix m;
    m.Translate(x, y);
    m.Scale(s / 24.0f, s / 24.0f);
    path.Transform(&m);

    g.DrawPath(&pen, &path);
}
