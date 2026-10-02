/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform datagram test declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _KMTEST_WFP_UDP_H_
#define _KMTEST_WFP_UDP_H_

#define IOCTL_WFPUDP_SETUP      1
#define IOCTL_WFPUDP_OBSERVED   2
#define IOCTL_WFPUDP_BLOCKED    3
#define IOCTL_WFPUDP_REMOVE     4
#define IOCTL_WFPUDP_REMOVED    5
#define IOCTL_WFPUDP_REDIRECT   6
#define IOCTL_WFPUDP_REDIRECTED 7
#define IOCTL_WFPUDP_CLOSED     8
#define IOCTL_WFPUDP_TEARDOWN   9
#define IOCTL_WFPUDP_REDIRECT_LOCAL   10
#define IOCTL_WFPUDP_REDIRECTED_LOCAL 11

#define WFPUDP_SERVER_PORT 47321
#define WFPUDP_PROXY_PORT  47322
#define WFPUDP_PLAIN_PORT  47323

#endif
