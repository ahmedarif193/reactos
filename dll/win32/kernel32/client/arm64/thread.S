/*
 * COPYRIGHT:  See COPYING in the top level directory
 * PROJECT:    ReactOS system libraries
 * FILE:       dll/win32/kernel32/client/arm64/thread.S
 * PURPOSE:    Thread and process start thunks for ARM64
 * PROGRAMMER: ReactOS ARM64 Port
 */

    .text
    .align 2

    .global BaseThreadStartupThunk
BaseThreadStartupThunk:
    /* TODO: Implement proper thread startup for ARM64 */
    /* For now, just return */
    ret

    .global BaseProcessStartThunk
BaseProcessStartThunk:
    /* TODO: Implement proper process startup for ARM64 */
    /* For now, just return */
    ret

/* EOF */