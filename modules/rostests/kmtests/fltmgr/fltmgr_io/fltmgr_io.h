/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager I/O test declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _KMTEST_FLTMGR_IO_H_
#define _KMTEST_FLTMGR_IO_H_

#define IOCTL_FLTIO_REGISTER    1
#define IOCTL_FLTIO_CHECK       2
#define IOCTL_FLTIO_UNREGISTER  3

#define FLTIO_PORT_NAME         L"\\FltMgrIoPort"
#define FLTIO_CONNECT_CONTEXT   "hello"

#define FLTIO_MSG_PING          1
#define FLTIO_MSG_SEND          2
#define FLTIO_MSG_SEND_NOREPLY  3

typedef struct _FLTIO_MESSAGE
{
    ULONG Command;
    ULONG Value;
} FLTIO_MESSAGE, *PFLTIO_MESSAGE;

#endif
