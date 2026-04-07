/*
 * nmrapi.h
 *
 * Network Module Registrar — client/provider registration entry points.
 * On ReactOS these live in netioddk.h (the kernel netio API). This file
 * is a thin forwarder so that Win7-style callers can write
 * `#include <nmrapi.h>` and pick up the same types they would get from
 * the Microsoft DDK.
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Created for the dev-nt6-1 branch as part of the NT 5.2 -> NT 6.1 upgrade.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 */

#pragma once
#ifndef _NMRAPI_
#define _NMRAPI_

#include <netioddk.h>

#endif /* _NMRAPI_ */

/* EOF */
