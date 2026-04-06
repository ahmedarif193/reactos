
#pragma once

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

/* NTDDI_VERSION is set globally from REACTOS_TARGET_NT (and overridden to
 * 0x600 here by CMakeLists.txt). Do not force WS03SP1 — that would silently
 * downgrade NDK struct layouts and create the same ABI mismatches that hid
 * RAM in System Properties (see kernel32/k32.h fix). */

#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>

/* CSRSS Headers */
#include <win/base.h>

/* Internal Kernel32 Header */
#include "../include/kernel32.h"
