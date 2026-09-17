/*
 * ReactOS cyclic WaveRT buffer scheduling.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __REACTOS_WAVERT_H
#define __REACTOS_WAVERT_H

#define REACTOS_RT_NOTIFICATION_COUNT 2

struct reactos_wavert_position
{
    UINT32 playing;
    UINT32 released;
    BOOL advanced;
    BOOL writable;
};

/* A notification is a wakeup, not a count of completed periods.  In particular,
 * an auto-reset event can merge two completions into one wakeup.  Always use
 * the DMA cursors to select the released half, including on a timeout or a
 * producer wakeup.  Repeated cursor samples must not retire/refill it twice. */
static BOOL reactos_wavert_cyclic_position(UINT32 period_frames, UINT32 frame_size,
                                         UINT32 previous_period, UINT64 play_offset,
                                         UINT64 write_offset,
                                         struct reactos_wavert_position *position)
{
    UINT64 period_bytes = (UINT64)period_frames * frame_size;

    if (!period_bytes || period_bytes > ~(UINT64)0 / REACTOS_RT_NOTIFICATION_COUNT ||
        previous_period >= REACTOS_RT_NOTIFICATION_COUNT ||
        play_offset >= period_bytes * REACTOS_RT_NOTIFICATION_COUNT ||
        write_offset >= period_bytes * REACTOS_RT_NOTIFICATION_COUNT)
        return FALSE;

    position->playing = play_offset / period_bytes;
    position->released = (position->playing + 1) % REACTOS_RT_NOTIFICATION_COUNT;
    position->advanced = position->playing != previous_period;
    /* The entire released half must be outside the controller's prefetch
     * interval.  A write cursor behind the play cursor has wrapped around. */
    position->writable = write_offset >= play_offset &&
                         write_offset / period_bytes == position->playing;
    return TRUE;
}

#endif /* __REACTOS_WAVERT_H */
