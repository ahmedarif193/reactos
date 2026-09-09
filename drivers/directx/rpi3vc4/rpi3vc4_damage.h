/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#pragma once

/* Preserve disjoint damage across scanout-buffer rotation. A single bounding
 * box turns a taskbar update plus a window update into a whole-screen copy. */
#define RPI3VC4_DAMAGE_RECTS 8

typedef struct _RPI3VC4_DAMAGE
{
    ULONG Count;
    RECT Rects[RPI3VC4_DAMAGE_RECTS];
} RPI3VC4_DAMAGE;

static VOID
Rpi3Vc4AddDamageRect(RPI3VC4_DAMAGE *Damage, const RECT *Rect)
{
    ULONGLONG Area, BestGrowth = ~(ULONGLONG)0;
    ULONG Index, Best = 0;
    RECT Union, BestUnion = *Rect;

    if (Rect->left >= Rect->right || Rect->top >= Rect->bottom)
        return;
    Area = (ULONGLONG)(Rect->right - Rect->left) * (Rect->bottom - Rect->top);
    for (Index = 0; Index < Damage->Count; ++Index)
    {
        const RECT *Old = &Damage->Rects[Index];
        ULONGLONG OldArea, UnionArea, Growth;

        Union.left = min(Old->left, Rect->left);
        Union.top = min(Old->top, Rect->top);
        Union.right = max(Old->right, Rect->right);
        Union.bottom = max(Old->bottom, Rect->bottom);
        OldArea = (ULONGLONG)(Old->right - Old->left) * (Old->bottom - Old->top);
        UnionArea = (ULONGLONG)(Union.right - Union.left) * (Union.bottom - Union.top);
        if (UnionArea <= OldArea + Area)
        {
            Damage->Rects[Index] = Union;
            return;
        }
        Growth = UnionArea - OldArea;
        if (Growth < BestGrowth)
        {
            BestGrowth = Growth;
            Best = Index;
            BestUnion = Union;
        }
    }
    if (Damage->Count < RPI3VC4_DAMAGE_RECTS)
        Damage->Rects[Damage->Count++] = *Rect;
    else
        Damage->Rects[Best] = BestUnion;
}
