/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform stream test declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _KMTEST_WFP_TCP_H_
#define _KMTEST_WFP_TCP_H_

#define IOCTL_WFPTCP_SETUP     1
#define IOCTL_WFPTCP_CONNECTED 2
#define IOCTL_WFPTCP_EXCHANGED 3
#define IOCTL_WFPTCP_CLOSED    4
#define IOCTL_WFPTCP_TEARDOWN  5
#define IOCTL_WFPTCP_SHUTDOWN  6

#define WFPTCP_SERVER_PORT 47341

#endif
