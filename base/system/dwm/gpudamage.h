/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Damage history for WGL buffers with a queried preservation contract. */
#pragma once

typedef struct _DWM_GPU_DAMAGE
{
    RECT Previous, Current, Draw;
    ULONG ValidFrames;
    INT SwapMethod;
} DWM_GPU_DAMAGE;

static RECT
DwmGpuDamageBegin(DWM_GPU_DAMAGE *State, LONG Width, LONG Height,
                   const RECT *Damage)
{
    RECT Full = {0, 0, Width, Height};
    RECT Current = Damage != NULL ? *Damage : Full;

    if (Current.left < 0) Current.left = 0;
    if (Current.top < 0) Current.top = 0;
    if (Current.right > Width) Current.right = Width;
    if (Current.bottom > Height) Current.bottom = Height;
    if (Current.right <= Current.left || Current.bottom <= Current.top)
        Current = Full;
    State->Current = Current;
    State->Draw = Full;
    if (State->SwapMethod == WGL_SWAP_COPY_ARB && State->ValidFrames != 0)
        State->Draw = Current;
    else if (State->SwapMethod == WGL_SWAP_EXCHANGE_ARB && State->ValidFrames >= 2)
    {
        /* The back buffer is two presents old. Repair the previous damage
         * as well as this frame's damage; never assume it contains the last
         * front buffer. Both buffers need a full initialization first. */
        State->Draw.left = min(Current.left, State->Previous.left);
        State->Draw.top = min(Current.top, State->Previous.top);
        State->Draw.right = max(Current.right, State->Previous.right);
        State->Draw.bottom = max(Current.bottom, State->Previous.bottom);
    }
    return State->Draw;
}

static void
DwmGpuDamageEnd(DWM_GPU_DAMAGE *State, BOOL Success)
{
    if (!Success)
        State->ValidFrames = 0;
    else
    {
        State->Previous = State->Current;
        if (State->ValidFrames < 2)
            ++State->ValidFrames;
    }
}
