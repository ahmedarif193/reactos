/*
 * COPYRIGHT:  See COPYING in the top level directory
 * PROJECT:    ReactOS system libraries
 * FILE:       dll/win32/kernel32/client/arm64/fiber.S
 * PURPOSE:    Fiber startup thunk for ARM64
 * PROGRAMMER: ReactOS ARM64 Port
 */

    .text
    .align 2

    .global BaseFiberStartup
BaseFiberStartup:
    /* TODO: Implement proper fiber startup for ARM64 */
    /* For now, just return */
    ret

    .global SwitchToFiber
SwitchToFiber:
    /* TODO: Implement proper fiber switching for ARM64 */
    /* For now, just return */
    ret

/* EOF */