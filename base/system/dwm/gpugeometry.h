/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Destination geometry is independent of the retained window texture size. */
#pragma once

typedef struct _DWM_GPU_WINDOW_GEOMETRY
{
    LONGLONG Left, Top;
    LONG Width, Height;
} DWM_GPU_WINDOW_GEOMETRY;

static BOOL
DwmGpuWindowGeometry(const DWM_WIN *Window, LONG OriginX, LONG OriginY,
                      DWM_GPU_WINDOW_GEOMETRY *Geometry)
{
    if (Window->cx <= 0 || Window->cy <= 0)
        return FALSE;
    if (Window->AnimFlags != 0)
    {
        Geometry->Left = (LONGLONG)Window->AnimX - OriginX;
        Geometry->Top = (LONGLONG)Window->AnimY - OriginY;
        Geometry->Width = Window->AnimCx;
        Geometry->Height = Window->AnimCy;
    }
    else
    {
        Geometry->Left = (LONGLONG)Window->x - OriginX;
        Geometry->Top = (LONGLONG)Window->y - OriginY;
        Geometry->Width = Window->cx;
        Geometry->Height = Window->cy;
    }
    return Geometry->Width > 0 && Geometry->Height > 0;
}

static LONGLONG
DwmGpuScaleWindowEdge(LONGLONG Edge, LONG SourceSize, LONG DestinationSize)
{
    return Edge * DestinationSize / SourceSize;
}

static BOOL
DwmGpuClientGeometry(const DWM_WIN *Window, const DWM_GPU_WINDOW_GEOMETRY *Owner,
                      DWM_GPU_WINDOW_GEOMETRY *Client)
{
    LONGLONG Left = Window->DxClientX, Top = Window->DxClientY;
    LONGLONG Right = Left + Window->DxWidth, Bottom = Top + Window->DxHeight;

    if (Window->DxWidth == 0 || Window->DxHeight == 0 ||
        Window->DxWidth > MAXLONG || Window->DxHeight > MAXLONG)
        return FALSE;
    if (Window->AnimFlags != 0)
    {
        Left = DwmGpuScaleWindowEdge(Left, Window->cx, Owner->Width);
        Top = DwmGpuScaleWindowEdge(Top, Window->cy, Owner->Height);
        Right = DwmGpuScaleWindowEdge(Right, Window->cx, Owner->Width);
        Bottom = DwmGpuScaleWindowEdge(Bottom, Window->cy, Owner->Height);
    }
    if (Right <= Left || Bottom <= Top || Right - Left > MAXLONG || Bottom - Top > MAXLONG)
        return FALSE;
    Client->Left = Owner->Left + Left;
    Client->Top = Owner->Top + Top;
    Client->Width = (LONG)(Right - Left);
    Client->Height = (LONG)(Bottom - Top);
    return TRUE;
}
