
#pragma once

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

/* Force NTDDI_VERSION to Vista so the NDK exposes NT6 entry points */
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_VISTA

#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/extypes.h>

/* CSRSS Headers */
#include <win/base.h>

/* Internal Kernel32 Header */
#include "../include/kernel32.h"

PUNICODE_STRING
K32VistaAnsiToStaticUnicode(LPCSTR Name);
