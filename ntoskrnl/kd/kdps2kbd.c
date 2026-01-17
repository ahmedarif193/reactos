/*
 * PROJECT:     ReactOS KDBG Kernel Debugger Terminal Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PS/2 Keyboard driver
 * COPYRIGHT:   Copyright 2001 David Welch <welch@cwcom.net>
 *              Copyright 2004 Art Yerkes <ayerkes@speakeasy.net>
 *              Copyright 2005 Gregor Anich <blight@blight.eu.org>
 *              Copyright 2009-2011 Dmitry Gorbachev <gorbachev@reactos.org>
 *
 * NOTE: Adapted from historic PS/2 keyboard driver authored by
 * Victor Kirhenshtein <sauros@iname.com> and Jason Filby <jasonfilby@yahoo.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

/*
 * ARM64 does not have traditional x86 I/O ports or a PS/2 controller.
 * Keyboard input on ARM64 typically comes through memory-mapped I/O devices.
 * Skip PS/2 controller operations on ARM64 to avoid hangs.
 */
#if defined(_M_ARM64) || defined(__aarch64__)
#define KBD_PS2_SUPPORTED 0
#else
#define KBD_PS2_SUPPORTED 1
#endif

#if KBD_PS2_SUPPORTED

#define KBD_STATUS_REG          0x64
#define KBD_CNTL_REG            0x64
#define KBD_DATA_REG            0x60

#define KBD_STAT_OBF            0x01
#define KBD_STAT_IBF            0x02

#define CTRL_WRITE_MOUSE        0xD4
#define MOU_ENAB                0xF4
#define MOU_DISAB               0xF5
#define MOUSE_ACK               0xFA

#define KBD_DISABLE_MOUSE       0xA7
#define KBD_ENABLE_MOUSE        0xA8

#define kbd_write_command(cmd)  WRITE_PORT_UCHAR((PUCHAR)KBD_CNTL_REG,cmd)
#define kbd_write_data(cmd)     WRITE_PORT_UCHAR((PUCHAR)KBD_DATA_REG,cmd)
#define kbd_read_input()        READ_PORT_UCHAR((PUCHAR)KBD_DATA_REG)
#define kbd_read_status()       READ_PORT_UCHAR((PUCHAR)KBD_STATUS_REG)

#endif /* KBD_PS2_SUPPORTED */

#if KBD_PS2_SUPPORTED
static unsigned char keyb_layout[2][128] =
{
    "\000\0331234567890-=\177\t"                                        /* 0x00 - 0x0f */
    "qwertyuiop[]\r\000as"                                              /* 0x10 - 0x1f */
    "dfghjkl;'`\000\\zxcv"                                              /* 0x20 - 0x2f */
    "bnm,./\000*\000 \000\201\202\203\204\205"                          /* 0x30 - 0x3f */
    "\206\207\210\211\212\000\000789-456+1"                             /* 0x40 - 0x4f */
    "230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000"   /* 0x50 - 0x5f */
    "\r\000/"                                                           /* 0x60 - 0x6f */
    ,
    "\000\033!@#$%^&*()_+\177\t"                                        /* 0x00 - 0x0f */
    "QWERTYUIOP{}\r\000AS"                                              /* 0x10 - 0x1f */
    "DFGHJKL:\"`\000\\ZXCV"                                             /* 0x20 - 0x2f */
    "BNM<>?\000*\000 \000\201\202\203\204\205"                          /* 0x30 - 0x3f */
    "\206\207\210\211\212\000\000789-456+1"                             /* 0x40 - 0x4f */
    "230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000"   /* 0x50 - 0x5f */
    "\r\000/"                                                           /* 0x60 - 0x6f */
};
#endif /* KBD_PS2_SUPPORTED */

typedef UCHAR byte_t;

/* FUNCTIONS *****************************************************************/

#if KBD_PS2_SUPPORTED

static VOID
KbdSendCommandToMouse(UCHAR Command)
{
    ULONG Retry = 20000;

    while (kbd_read_status() & KBD_STAT_OBF && Retry--)
    {
        kbd_read_input();
        KeStallExecutionProcessor(50);
    }

    Retry = 20000;
    while (kbd_read_status() & KBD_STAT_IBF && Retry--)
        KeStallExecutionProcessor(50);

    kbd_write_command(CTRL_WRITE_MOUSE);

    Retry = 20000;
    while (kbd_read_status() & KBD_STAT_IBF && Retry--)
        KeStallExecutionProcessor(50);

    kbd_write_data(Command);

    Retry = 20000;
    while (!(kbd_read_status() & KBD_STAT_OBF) && Retry--)
        KeStallExecutionProcessor(50);

    if (kbd_read_input() != MOUSE_ACK) { ; }

    return;
}

VOID KbdEnableMouse(VOID)
{
    KbdSendCommandToMouse(MOU_ENAB);
}

VOID KbdDisableMouse(VOID)
{
    KbdSendCommandToMouse(MOU_DISAB);
}

CHAR
KdbpTryGetCharKeyboard(PULONG ScanCode, ULONG Retry)
{
    static byte_t last_key = 0;
    static byte_t shift = 0;
    char c;
    BOOLEAN KeepRetrying = (Retry == 0);

    while (KeepRetrying || Retry-- > 0)
    {
        while (kbd_read_status() & KBD_STAT_OBF)
        {
            byte_t scancode;

            scancode = kbd_read_input();

            /* check for SHIFT-keys */
            if (((scancode & 0x7F) == 42) || ((scancode & 0x7F) == 54))
            {
                shift = !(scancode & 0x80);
                continue;
            }

            /* ignore all other RELEASED-codes */
            if (scancode & 0x80)
            {
                last_key = 0;
            }
            else if (last_key != scancode)
            {
                //printf("kbd: %d, %d, %c\n", scancode, last_key, keyb_layout[shift][scancode]);
                last_key = scancode;
                c = keyb_layout[shift][scancode];
                *ScanCode = scancode;

                if (c > 0)
                    return c;
            }
        }
    }

    return -1;
}

#else /* !KBD_PS2_SUPPORTED (ARM64) */

/*
 * ARM64 stub implementations - no PS/2 controller available.
 * These functions are no-ops on ARM64 to avoid hanging on I/O port access.
 */
VOID KbdEnableMouse(VOID)
{
    /* No-op on ARM64 - no PS/2 controller */
}

VOID KbdDisableMouse(VOID)
{
    /* No-op on ARM64 - no PS/2 controller */
}

CHAR
KdbpTryGetCharKeyboard(PULONG ScanCode, ULONG Retry)
{
    UNREFERENCED_PARAMETER(ScanCode);
    UNREFERENCED_PARAMETER(Retry);
    /* ARM64 does not have PS/2 keyboard - always return no input */
    return -1;
}

#endif /* KBD_PS2_SUPPORTED */

/* EOF */
